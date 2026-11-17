#!/usr/bin/env python3
"""Repor OpusHead ausente em cópias de WebM mono/estéreo, sem recodificação."""
import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

VERSION = "1.0.0"
SEGMENT, TRACKS, ENTRY = 0x18538067, 0x1654AE6B, 0xAE
CRC, SEEKHEAD, CUES, CLUSTER = 0xBF, 0x114D9B74, 0x1C53BB6B, 0x1F43B675
MAX_TRACKS = 16 * 1024 * 1024


class RepairError(Exception):
    pass


@dataclass
class Element:
    ident: int
    start: int
    data: int
    end: int
    unknown: bool


def read_vint(f, identifier=False):
    first = f.read(1)
    if not first or first == b"\0":
        raise RepairError("Cabeçalho EBML truncado ou inválido.")
    width = 9 - first[0].bit_length()
    if width > (4 if identifier else 8):
        raise RepairError("Comprimento EBML inválido.")
    rest = f.read(width - 1)
    if len(rest) != width - 1:
        raise RepairError("Cabeçalho EBML incompleto.")
    value = int.from_bytes(first + rest, "big")
    if not identifier:
        value &= (1 << (7 * width)) - 1
    return value, width


def elements(f, start, end):
    pos = start
    while pos < end:
        f.seek(pos)
        ident, _ = read_vint(f, True)
        size, width = read_vint(f)
        unknown = size == (1 << (7 * width)) - 1
        data = f.tell()
        stop = end if unknown else data + size
        if data > end or stop > end:
            raise RepairError("Elemento EBML ultrapassa o fim do ficheiro/bloco.")
        if unknown and ident != SEGMENT:
            raise RepairError("Elemento de tamanho desconhecido: captura em fluxo não suportada.")
        yield Element(ident, pos, data, stop, unknown)
        pos = stop


def encode(ident, payload):
    width = 1
    while len(payload) >= (1 << (7 * width)) - 1:
        width += 1
    ident_bytes = ident.to_bytes((ident.bit_length() + 7) // 8, "big")
    return ident_bytes + ((1 << (7 * width)) | len(payload)).to_bytes(width, "big") + payload


def fields(payload):
    f = io.BytesIO(payload)
    return [(e.ident, payload[e.data:e.end]) for e in elements(f, 0, len(payload))]


def unique(items, key, default=None):
    values = [value for ident, value in items if ident == key]
    if len(values) > 1:
        raise RepairError(f"Campo EBML duplicado: {key:x}.")
    return values[0] if values else default


def uint(payload):
    if payload is None or not 1 <= len(payload) <= 8:
        raise RepairError("Valor inteiro em falta ou inválido.")
    return int.from_bytes(payload, "big")


def repair_tracks(payload):
    result, repairs = [], []
    for ident, value in fields(payload):
        if ident != ENTRY:
            result.append((ident, value))
            continue
        entry = fields(value)
        if unique(entry, 0x86) != b"A_OPUS":
            result.append((ident, value))
            continue
        if unique(entry, 0x6D80) is not None:
            raise RepairError("Opus com compressão/encriptação adicional não suportado.")
        private = unique(entry, 0x63A2)
        if private:
            if len(private) < 19 or not private.startswith(b"OpusHead"):
                raise RepairError("CodecPrivate Opus presente mas inválido; não será substituído.")
            result.append((ident, value))
            continue
        number = uint(unique(entry, 0xD7))
        audio = fields(unique(entry, 0xE1, b""))
        channels = uint(unique(audio, 0x9F))
        if channels not in (1, 2):
            raise RepairError(f"Faixa {number}: {channels} canais; falta o mapa surround, não inferível.")
        delay = uint(unique(entry, 0x56AA))
        # CodecDelay está em ns; pre-skip está sempre em amostras a 48 kHz.
        preskip = (delay * 48000 + 500_000_000) // 1_000_000_000
        if preskip > 65535 or abs(delay * 48000 - preskip * 1_000_000_000) > 48000:
            raise RepairError(f"Faixa {number}: CodecDelay não representa um pre-skip válido.")
        rate_bytes = unique(audio, 0xB5)
        if rate_bytes is None or len(rate_bytes) not in (4, 8):
            raise RepairError(f"Faixa {number}: SamplingFrequency em falta/inválida.")
        rate = struct.unpack(">f" if len(rate_bytes) == 4 else ">d", rate_bytes)[0]
        if not math.isfinite(rate) or rate != int(rate) or not 1 <= rate <= 0xFFFFFFFF:
            raise RepairError(f"Faixa {number}: frequência de amostragem inválida.")
        header = b"OpusHead" + struct.pack("<BBHIhB", 1, channels, preskip, int(rate), 0, 0)
        cleaned = [(k, v) for k, v in entry if k not in (CRC, 0x63A2)]
        cleaned.append((0x63A2, header))
        result.append((ident, b"".join(encode(k, v) for k, v in cleaned)))
        repairs.append({"faixa": number, "canais": channels, "pre_skip": preskip,
                        "frequencia": int(rate)})
    if not repairs:
        return payload, []
    return b"".join(encode(k, v) for k, v in result if k != CRC), repairs


def inspect(path):
    size = path.stat().st_size
    with path.open("rb") as f:
        top = list(elements(f, 0, size))
        if not top or top[0].ident != 0x1A45DFA3:
            raise RepairError("O ficheiro não tem um cabeçalho EBML.")
        header = top[0]
        if header.end - header.data > 65536:
            raise RepairError("Cabeçalho EBML demasiado grande.")
        f.seek(header.data)
        if unique(fields(f.read(header.end - header.data)), 0x4282) != b"webm":
            raise RepairError("O contentor não está identificado como WebM.")
        segments = [e for e in top if e.ident == SEGMENT]
        if len(segments) != 1:
            raise RepairError("É necessário um único segmento WebM.")
        segment = segments[0]
        tracks = None
        for e in elements(f, segment.data, segment.end):
            if e.ident == CLUSTER:
                break
            if e.ident == TRACKS:
                if tracks is not None:
                    raise RepairError("Foram encontrados blocos Tracks duplicados.")
                tracks = e
        if tracks is None or tracks.end - tracks.data > MAX_TRACKS:
            raise RepairError("Tracks ausente antes do vídeo ou demasiado grande.")
        f.seek(tracks.data)
        repaired, changes = repair_tracks(f.read(tracks.end - tracks.data))
    return top, segment, tracks, repaired, changes


def copy_range(src, dst, start, end):
    src.seek(start)
    remaining = end - start
    while remaining:
        block = src.read(min(4 * 1024 * 1024, remaining))
        if not block:
            raise RepairError("O ficheiro mudou ou ficou truncado durante a cópia.")
        dst.write(block)
        remaining -= len(block)


def patched_copy(source, target, inspection):
    top, segment, tracks, repaired, _ = inspection
    with source.open("rb") as src, target.open("xb") as dst:
        for e in top:
            if e.ident != SEGMENT:
                copy_range(src, dst, e.start, e.end)
                continue
            # Índices antigos deixam de apontar para os offsets certos. FFmpeg recria-os.
            dst.write(b"\x18\x53\x80\x67\x01\xff\xff\xff\xff\xff\xff\xff")
            for child in elements(src, segment.data, segment.end):
                if child.ident in (CRC, SEEKHEAD, CUES):
                    continue
                if child.ident == TRACKS:
                    if child.start != tracks.start:
                        raise RepairError("Mais de um bloco Tracks não suportado.")
                    dst.write(encode(TRACKS, repaired))
                else:
                    copy_range(src, dst, child.start, child.end)


def run(command):
    p = subprocess.run(command, capture_output=True, text=True)
    if p.returncode:
        raise RepairError((p.stderr or p.stdout or "Falha na ferramenta externa.")[-3000:].strip())
    return p.stdout


def packet_signatures(path):
    """SHA256 incremental dos hashes dos pacotes, por faixa: memória constante."""
    counts, hashes, timing = defaultdict(int), {}, {}
    with tempfile.TemporaryFile(mode="w+t") as errors:
        p = subprocess.Popen(["ffprobe", "-v", "error", "-show_packets", "-show_data_hash", "sha256",
                              "-show_entries", "packet=stream_index,data_hash,pts_time,dts_time,duration_time:packet_side_data=", "-of",
                              "compact=p=0:nk=0", str(path)], stdout=subprocess.PIPE,
                             stderr=errors, text=True)
        try:
            for line in p.stdout:
                values = dict(part.split("=", 1) for part in line.strip().split("|") if "=" in part)
                if not line.strip():
                    continue
                if "stream_index" not in values or "data_hash" not in values:
                    raise RepairError("FFprobe devolveu um pacote sem índice/hash; validação interrompida.")
                idx = int(values["stream_index"])
                digest = values["data_hash"]
                if not digest.startswith("SHA256:"):
                    raise RepairError("FFprobe não devolveu o hash SHA256 esperado.")
                hashes.setdefault(idx, hashlib.sha256()).update(bytes.fromhex(digest[7:]))
                times = [values.get(key) for key in ("pts_time", "dts_time", "duration_time")]
                timing.setdefault(idx, hashlib.sha256()).update(json.dumps(times).encode() + b"\n")
                counts[idx] += 1
            code = p.wait()
            errors.seek(0)
            diagnostic = errors.read()
            if code or diagnostic:
                raise RepairError("Não foi possível validar todos os pacotes: " + diagnostic[-2000:])
        finally:
            if p.poll() is None:
                p.kill()
            p.wait()
            p.stdout.close()
    if not counts:
        raise RepairError("Não foram encontrados pacotes para validar.")
    return {str(k): {"pacotes": counts[k], "sha256": v.hexdigest(),
                     "tempos_sha256": timing[k].hexdigest()} for k, v in hashes.items()}


def audio_signatures(path):
    output = run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(path), "-map", "0:a",
                  "-c:a", "pcm_s16le", "-f", "streamhash", "-hash", "sha256", "-"])
    lines = [line for line in output.splitlines() if line and not line.startswith("#")]
    if not lines or any(",a,SHA256=" not in line for line in lines):
        raise RepairError("Não foi possível validar o áudio descodificado.")
    return lines


def fingerprint(path):
    s = path.stat()
    return s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns


def repair(source, output=None, check_only=False):
    source = source.resolve(strict=True)
    before = fingerprint(source)
    inspection = inspect(source)
    changes = inspection[-1]
    record = {"origem": str(source), "faixas": changes}
    if not changes:
        return dict(record, estado="sem_cabecalho_em_falta")
    if check_only:
        return dict(record, estado="reparavel")
    folder = output if output else source.parent / "WebM corrigidos"
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / source.name
    if target.exists() or target.is_symlink():
        raise RepairError(f"O destino já existe; nada foi sobrescrito: {target}")
    with tempfile.TemporaryDirectory(prefix=".reparar-webm-", dir=folder) as temporary:
        temp = Path(temporary)
        injected, remuxed = temp / "cabecalho.webm", temp / "validado.webm"
        patched_copy(source, injected, inspection)
        run(["ffmpeg", "-nostdin", "-v", "error", "-n", "-copyts", "-i", str(injected), "-map", "0",
             "-map_metadata", "0", "-map_chapters", "0", "-c", "copy",
             "-avoid_negative_ts", "disabled", str(remuxed)])
        signatures = packet_signatures(source)
        if signatures != packet_signatures(remuxed):
            raise RepairError("Os pacotes ou tempos de áudio/vídeo diferem; cópia rejeitada.")
        pcm = audio_signatures(source)
        if pcm != audio_signatures(remuxed):
            raise RepairError("O áudio descodificado difere da origem; cópia rejeitada.")
        if inspect(remuxed)[-1]:
            raise RepairError("O cabeçalho reparado não foi conservado na saída.")
        probe = json.loads(run(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(remuxed)]))
        if any(s.get("extradata_size", 0) < 19 for s in probe["streams"] if s["codec_name"] == "opus"):
            raise RepairError("FFprobe não reconhece o cabeçalho Opus na saída.")
        if fingerprint(source) != before:
            raise RepairError("A origem mudou durante a operação; cópia rejeitada.")
        # Publicação atómica sem substituir um ficheiro criado entretanto.
        os.link(remuxed, target)
    return dict(record, estado="corrigido", destino=str(target), verificacao=signatures, pcm_sha256=pcm)


def collect(paths, recursive=False):
    found = []
    for name in paths:
        p = Path(name).expanduser().absolute()
        if p.is_dir():
            candidates = p.rglob("*") if recursive else p.iterdir()
            for f in sorted(candidates):
                if f.is_file() and f.suffix.lower() == ".webm" and "WebM corrigidos" not in f.relative_to(p).parts:
                    found.append(f)
        elif p.is_file() and p.suffix.lower() == ".webm":
            found.append(p)
        else:
            raise RepairError(f"Ficheiro/pasta WebM não encontrado: {p}")
    return list(dict.fromkeys(p.resolve() for p in found))


def choose_files():
    if not shutil.which("zenity"):
        raise RepairError("Para o seletor é necessário zenity; também podes passar ficheiros no terminal.")
    p = subprocess.run(["zenity", "--file-selection", "--multiple", "--separator=\n",
                        "--title=Escolher WebM para verificar e reparar",
                        "--file-filter=Vídeos WebM | *.webm *.WEBM"], capture_output=True, text=True)
    if p.returncode == 1:
        return []
    if p.returncode:
        raise RepairError("Não foi possível abrir o seletor de ficheiros.")
    return p.stdout.rstrip("\n").split("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("caminhos", nargs="*", help="ficheiros WebM ou pastas")
    parser.add_argument("--verificar", action="store_true", help="diagnosticar sem criar cópias")
    parser.add_argument("--recursivo", action="store_true", help="incluir subpastas")
    parser.add_argument("--saida", type=Path, help="pasta de destino; padrão: WebM corrigidos junto à origem")
    parser.add_argument("--relatorio", type=Path, help="guardar relatório JSON num ficheiro novo")
    parser.add_argument("--escolher", action="store_true", help="abrir seletor de ficheiros")
    parser.add_argument("--version", action="version", version=VERSION)
    args = parser.parse_args(argv)
    records, failed = [], False
    try:
        if args.relatorio and (args.relatorio.exists() or args.relatorio.is_symlink()):
            raise RepairError("O relatório de destino já existe.")
        for executable in ("ffmpeg", "ffprobe"):
            if not shutil.which(executable):
                raise RepairError(f"Falta instalar {executable}.")
        names = args.caminhos + (choose_files() if args.escolher or not args.caminhos else [])
        if not names:
            return 0
        paths = collect(names, args.recursivo)
        if not paths:
            raise RepairError("Não foram encontrados ficheiros .webm.")
        for source in paths:
            print(f"A verificar: {source}", flush=True)
            try:
                record = repair(source, args.saida.absolute() if args.saida else None, args.verificar)
                status = record["estado"]
                if status == "corrigido":
                    print(f"  CORRIGIDO e validado: {record['destino']}")
                elif status == "reparavel":
                    print("  REPARÁVEL: cabeçalho Opus em falta.")
                else:
                    print("  SEM ALTERAÇÃO: não foi detetado este problema.")
            except (RepairError, OSError, ValueError) as exc:
                record = {"origem": str(source), "estado": "erro", "motivo": str(exc)}
                print(f"  NÃO ALTERADO: {exc}", file=sys.stderr)
                failed = True
            records.append(record)
        if args.relatorio:
            with args.relatorio.open("x") as f:
                json.dump({"versao": VERSION, "ficheiros": records}, f, ensure_ascii=False, indent=2)
        message = f"{sum(r['estado'] == 'corrigido' for r in records)} corrigido(s); " \
                  f"{sum(r['estado'] == 'reparavel' for r in records)} reparável(eis); " \
                  f"{sum(r['estado'] == 'sem_cabecalho_em_falta' for r in records)} sem este problema; " \
                  f"{sum(r['estado'] == 'erro' for r in records)} erro(s)."
        print(message)
        if args.escolher or not args.caminhos:
            subprocess.run(["zenity", "--warning" if failed else "--info", "--no-markup",
                            "--title=Reparação WebM", "--text=" + message +
                            "\n\nOs originais foram preservados. As cópias ficam em WebM corrigidos "
                            "junto aos originais, salvo escolha de outra pasta com --saida."])
        return 1 if failed else 0
    except (RepairError, OSError, ValueError) as exc:
        print(f"Erro: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrompido. Os originais foram preservados.", file=sys.stderr)
        sys.exit(130)
