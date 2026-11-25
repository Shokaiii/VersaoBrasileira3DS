#!/usr/bin/env python3
"""Gera um catálogo de teste com jogos fictícios, ícones 48x48 e capas 112x128.

Uso: python gerar_catalogo_teste.py <pasta-de-saida> <base_url>
A pasta de saída fica pronta para enviar ao servidor (version.json,
catalog.json e images/).
"""
import hashlib, io, json, os, sys, zipfile
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
BASE = sys.argv[2].rstrip('/') + '/'
VERSION = sys.argv[3] if len(sys.argv) > 3 else '1.2.0'

def font(size):
    for path in ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
                 'C:/Windows/Fonts/arialbd.ttf']:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()

GAMES = [
    ('aventura-do-cangaco', 'Aventura do Cangaço', '3ds', ['USA'], (196, 98, 45)),
    ('baia-dos-piratas', 'Baía dos Piratas 3D', '3ds', ['EUR'], (30, 110, 170)),
    ('cacadores-de-reliquias', 'Caçadores de Relíquias: O Templo Perdido', '3ds', ['USA', 'EUR'], (120, 80, 40)),
    ('detetive-noturno', 'Detetive Noturno', 'nds', ['USA'], (60, 50, 110)),
    ('estrelas-do-futebol', 'Estrelas do Futebol 2026', '3ds', ['EUR'], (40, 150, 70)),
    ('floresta-encantada', 'Floresta Encantada', '3ds', ['JPN'], (30, 120, 60)),
    ('guardioes-do-cristal', 'Guardiões do Cristal', '3ds', ['USA'], (110, 60, 160)),
    ('herois-do-sertao', 'Heróis do Sertão', 'nds', ['USA', 'EUR', 'JPN'], (200, 150, 40)),
    ('ilha-misteriosa', 'Ilha Misteriosa', '3ds', ['ALL'], (20, 140, 150)),
    ('jornada-espacial', 'Jornada Espacial: Além das Estrelas', '3ds', ['USA'], (20, 30, 70)),
    ('kart-turbo', 'Kart Turbo Racing', '3ds', ['EUR'], (200, 40, 40)),
    ('lendas-do-oceano', 'Lendas do Oceano', 'nds', ['USA'], (20, 80, 140)),
]

def initials(name):
    words = [w for w in name.replace(':', '').split() if w[0].isupper()]
    return ''.join(w[0] for w in words[:2])

def shade(color, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in color)

def make_icon(path, name, color):
    im = Image.new('RGB', (48, 48), shade(color, 0.6))
    d = ImageDraw.Draw(im)
    for y in range(48):
        d.line([(0, y), (47, y)], fill=shade(color, 1.25 - y / 80))
    text = initials(name)
    f = font(20)
    box = d.textbbox((0, 0), text, font=f)
    d.text(((48 - (box[2] - box[0])) / 2 - box[0], (48 - (box[3] - box[1])) / 2 - box[1]),
           text, font=f, fill=(255, 255, 255))
    im.save(path)

def make_cover(path, name, color):
    im = Image.new('RGB', (112, 128))
    d = ImageDraw.Draw(im)
    for y in range(128):
        d.line([(0, y), (111, y)], fill=shade(color, 1.3 - y / 140))
    d.rectangle([0, 0, 111, 14], fill=(240, 240, 240))
    d.text((5, 1), 'VERSÃO BR', font=font(10), fill=(23, 138, 74))
    f = font(13)
    words, lines, line = name.split(), [], ''
    for w in words:
        test = (line + ' ' + w).strip()
        if d.textlength(test, font=f) > 100 and line:
            lines.append(line); line = w
        else:
            line = test
    lines.append(line)
    y = 70 - len(lines) * 8
    for l in lines:
        d.text(((112 - d.textlength(l, font=f)) / 2, y), l, font=f, fill=(255, 255, 255))
        y += 17
    im.save(path, quality=90)

def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()

games = []
for index, (gid, name, platform, regions, color) in enumerate(GAMES):
    folder = os.path.join(OUT, 'images', gid)
    os.makedirs(folder, exist_ok=True)
    icon, cover = os.path.join(folder, 'icon.png'), os.path.join(folder, 'cover.jpg')
    make_icon(icon, name, color)
    make_cover(cover, name, color)
    title_id = '00040000%08X' % (0x00A00000 + index * 0x100)
    translations = []
    for t in range(2 if index % 4 == 0 else 1):
        tid = gid + ('-ptbr' if t == 0 else '-ptbr-revisada')
        tmp = 'sdmc:/3ds/VersaoBrasileira/tmp/%s/' % tid
        dest = 'sdmc:/luma/titles/%s/' % title_id
        files_dir = os.path.join(OUT, 'files', tid)
        os.makedirs(files_dir, exist_ok=True)

        def make_zip(name, entries):
            path = os.path.join(files_dir, name)
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
                for entry, text in entries.items():
                    z.writestr(entry, text)
            return path

        note = 'Arquivo de teste do Versão Brasileira (%s).\n' % tid
        if index == 2 and t == 0:
            # Exemplo 3: vários arquivos, extração temporária, cópia e exclusão.
            p1 = make_zip('parte1.zip', {'romfs/texto/menu.txt': note * 50,
                                         'romfs/texto/dialogo.txt': note * 80})
            p2 = make_zip('parte2.zip', {'romfs/graficos/titulo.txt': note * 20,
                                         'LEIA-ME.txt': note})
            steps = [
                {'type': 'downloadFile', 'file': 'files/%s/parte1.zip' % tid,
                 'output': tmp + 'parte1.zip', 'sha256': sha(p1)},
                {'type': 'downloadFile', 'file': 'files/%s/parte2.zip' % tid,
                 'output': tmp + 'parte2.zip', 'sha256': sha(p2)},
                {'type': 'extractFile', 'file': tmp + 'parte1.zip', 'output': tmp + 'x/'},
                {'type': 'extractFile', 'file': tmp + 'parte2.zip', 'output': tmp + 'x/'},
                {'type': 'copyFile', 'source': tmp + 'x/romfs/', 'destination': dest + 'romfs/'},
                {'type': 'deleteFile', 'file': tmp + 'parte1.zip'},
            ]
            size = os.path.getsize(p1) + os.path.getsize(p2)
        else:
            p = make_zip('patch.zip', {'romfs/LEIA-ME.txt': note,
                                       'romfs/texto/menu.txt': note * 40})
            steps = [
                {'type': 'downloadFile', 'file': 'files/%s/patch.zip' % tid,
                 'output': tmp + 'patch.zip', 'sha256': sha(p)},
                {'type': 'extractFile', 'file': tmp + 'patch.zip', 'output': dest},
            ]
            size = os.path.getsize(p)
        translations.append({
            'id': tid,
            'name': name + (' — Tradução PT-BR' if t == 0 else ' — Tradução Revisada'),
            'language': 'pt-BR',
            'version': '1.%d.0' % (index % 3 + t),
            'release_date': '2026-%02d-15' % (index % 12 + 1),
            'authors': [{'name': 'Equipe Teste', 'role': 'Tradução'},
                        {'name': 'Revisor Fictício', 'role': 'Revisão'}],
            'description': 'Tradução fictícia usada para testar o catálogo do Versão Brasileira. '
                           'Inclui textos, menus e gráficos traduzidos.',
            'changelog': [{'version': '1.0.0', 'notes': 'Primeira versão.'}],
            'compatibility': {
                'regions': regions,
                'game_versions': ['1.0'] if platform == 'nds' else ['1.2'],
                'consoles': 'new3ds' if index == 9 else 'all',
                'requirements': ['luma3ds'] if platform == '3ds' else [],
                'notes': 'Somente para teste.' if t == 0 else '',
            },
            'download_size': size,
            'install_size': size * 4,
            'install': {'steps': steps},
        })
    games.append({
        'id': gid, 'name': name, 'platform': platform,
        'description': 'Jogo fictício criado para testar o Versão Brasileira. '
                       'A descrição é longa de propósito, para conferir a quebra de linha '
                       'e a rolagem dos detalhes na tela inferior do console.',
        'publisher': 'Estúdio Exemplo', 'release_year': 2010 + index, 'genres': ['Aventura'],
        'title_ids': [{'region': r, 'id': title_id} for r in regions if r != 'ALL'],
        'images': {
            'icon': {'url': 'images/%s/icon.png' % gid, 'sha256': sha(icon), 'width': 48, 'height': 48},
            'cover': {'url': 'images/%s/cover.jpg' % gid, 'sha256': sha(cover), 'width': 112, 'height': 128},
        },
        'translations': translations,
    })

catalog = {'catalog_version': VERSION, 'generated_at': '2026-11-18T22:00:00Z',
           'base_url': BASE, 'games': games}
json.dump(catalog, open(os.path.join(OUT, 'catalog.json'), 'w', encoding='utf-8'),
          ensure_ascii=False, indent=2)
json.dump({'catalog_version': VERSION, 'catalog_url': BASE + 'catalog.json',
           'published_at': '2026-11-18T22:00:00Z'},
          open(os.path.join(OUT, 'version.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
print('ok', len(games), 'jogos')
