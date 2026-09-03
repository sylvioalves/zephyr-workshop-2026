#!/usr/bin/env python3
"""Publica uma imagem assinada e serve por HTTP para as placas da sala.

Pega o zephyr.signed.bin de um diretorio de build, copia para uma pasta
servida, sobe um servidor HTTP e imprime a URL que os participantes digitam.

    ./scripts/publicar-ota.py build/lab9-ota

O servidor e multi-thread, entao aguenta a sala inteira baixando ao mesmo
tempo. Encerre com Ctrl+C.
"""

import argparse
import http.server
import pathlib
import shutil
import socket
import struct
import sys


def ip_da_rede():
    """Descobre o IP que as placas enxergam, sem depender de hostname."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def versao_da_imagem(caminho):
    """Le a versao do cabecalho MCUboot, para conferir antes de publicar."""
    with open(caminho, "rb") as f:
        head = f.read(32)
    if len(head) < 32 or head[:4] != bytes.fromhex("3db8f396"):
        return None
    major, minor = head[20], head[21]
    rev = struct.unpack("<H", head[22:24])[0]
    build = struct.unpack("<I", head[24:28])[0]
    return "%d.%d.%d+%d" % (major, minor, rev, build)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", help="diretorio de build (ex: build/lab9-ota)")
    ap.add_argument("-p", "--port", type=int, default=8000)
    ap.add_argument("-n", "--nome", default="lab9.signed.bin",
                    help="nome do arquivo servido")
    ap.add_argument("-d", "--dir", default="ota-publicado",
                    help="pasta servida")
    args = ap.parse_args()

    origem = pathlib.Path(args.build) / "zephyr" / "zephyr.signed.bin"
    if not origem.is_file():
        # sysbuild coloca a imagem num subdiretorio com o nome da aplicacao
        candidatos = sorted(pathlib.Path(args.build).glob("*/zephyr/zephyr.signed.bin"))
        if not candidatos:
            sys.exit("nao achei zephyr.signed.bin em %s" % args.build)
        origem = candidatos[0]

    versao = versao_da_imagem(origem)
    if versao is None:
        sys.exit("%s nao parece uma imagem assinada do MCUboot" % origem)

    destino = pathlib.Path(args.dir)
    destino.mkdir(exist_ok=True)
    shutil.copy2(origem, destino / args.nome)

    tamanho = (destino / args.nome).stat().st_size
    url = "http://%s:%d/%s" % (ip_da_rede(), args.port, args.nome)

    print("imagem : %s" % origem)
    print("versao : %s" % versao)
    print("tamanho: %d bytes (%.0f KB)" % (tamanho, tamanho / 1024))
    print()
    print("na placa, digite:")
    print()
    print("    ota download %s" % url)
    print("    ota apply")
    print("    ota reboot")
    print()
    print("servindo %s na porta %d, Ctrl+C para parar" % (destino, args.port))
    print()

    handler = lambda *a, **kw: http.server.SimpleHTTPRequestHandler(
        *a, directory=str(destino), **kw)
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nparado")


if __name__ == "__main__":
    main()
