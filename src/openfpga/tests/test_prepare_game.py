"""Run the real data converters on tiny, redistributable GameMaker fixtures."""
import io
import json
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave
import zlib
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import prepare_game as prep


def u32(value):
    return struct.pack('<I', value)


def game_data(version=16, group=0, bad_audio=False):
    """One PNG page and one embedded PCM sound, with absolute GM pointers."""
    def png_chunk(name, payload):
        return u32(len(payload))[::-1] + name + payload + struct.pack('>I', zlib.crc32(name + payload))
    png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>2I5B', 1, 1, 8, 6, 0, 0, 0))
    png += png_chunk(b'IDAT', zlib.compress(b'\0\xff\0\0\xff')) + png_chunk(b'IEND', b'')
    audio = io.BytesIO()
    with wave.open(audio, 'wb') as wav:
        wav.setparams((1, 2, 32000, 0, 'NONE', 'not compressed'))
        wav.writeframes(struct.pack('<16h', *([1000, -1000] * 8)))
    sound = b'broken' if bad_audio else audio.getvalue()
    parts = []
    offset = 8
    def add(name, payload):
        nonlocal offset
        parts.append(name + u32(len(payload)) + payload)
        offset += 8 + len(payload)
    add(b'GEN8', bytes([0, version]) + b'\0' * 6)
    # TXTR: count, entry pointer, 8-byte entry, then a PNG.
    add(b'TXTR', u32(1) + u32(offset + 16) + u32(0) + u32(offset + 24) + png)
    # SOND: count, pointer, then 36-byte sound entry and its name.
    entry = offset + 16
    fields = [entry + 36, 3, 0, 0, 0, 0, 0, group, 0]
    add(b'SOND', u32(1) + u32(entry) + struct.pack('<9I', *fields) + b'tone\0')
    if group:
        add(b'AUDO', u32(0))
    else:
        add(b'AUDO', u32(1) + u32(offset + 16) + u32(len(sound)) + sound)
    payload = b''.join(parts)
    bank_payload = u32(1) + u32(24) + u32(len(sound)) + sound
    bank = b'FORM' + u32(8 + len(bank_payload)) + b'AUDO' + u32(len(bank_payload)) + bank_payload
    return b'FORM' + u32(len(payload)) + payload, bank


class Preparation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool_temp = tempfile.TemporaryDirectory()
        cls.tools = Path(cls.tool_temp.name)
        prep.build_tools(cls.tools)

    @classmethod
    def tearDownClass(cls):
        cls.tool_temp.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'my game'
        self.source.mkdir()
        self.output = self.root / 'prepared data'

    def prepare(self, game='undertale', source=None):
        return prep.prepare(game, source or self.source, self.output, self.tools)

    def test_real_undertale_conversion_and_combined_pack(self):
        data, _ = game_data()
        (self.source / 'data.win').write_bytes(data)
        self.prepare()
        common = self.output / 'Assets/butterscotch/common'
        self.assertEqual((common / 'data.win').read_bytes(), data)
        texture = (common / 'textures.bin').read_bytes()
        self.assertEqual(texture[:4], b'UTX2')
        music = (common / 'music.bin').read_bytes()
        self.assertEqual(music[:4], b'UTM1')
        start, size, reserved, magic = struct.unpack('<III4s', music[-16:])
        self.assertEqual(start % 4096, 0)
        self.assertEqual((size, reserved, magic), (len(texture), 0, b'UTXT'))
        self.assertEqual(music[start:-16], texture)
        self.assertFalse((self.output / 'Saves').exists())
        self.assertEqual(sorted(p.name for p in self.source.iterdir()), ['data.win'])
        self.assertEqual(json.loads((self.output / 'PREPARED-DATA.json').read_text())['game'], 'undertale')

    def test_mac_app_resource_discovery(self):
        app = self.source / 'UNDERTALE.app'
        resources = app / 'Contents/Resources'
        resources.mkdir(parents=True)
        (resources / 'game.ios').write_bytes(game_data()[0])
        self.prepare(source=app)
        self.assertTrue((self.output / 'Assets/butterscotch/common/data.win').exists())

    def test_deltarune_chapter_and_raw_language_are_packed(self):
        chapter = self.source / 'chapter1_windows'
        (chapter / 'lang').mkdir(parents=True)
        data, bank = game_data(17, group=1)
        (chapter / 'data.win').write_bytes(data)
        (chapter / 'audiogroup1.dat').write_bytes(bank)
        language = b'{"hello":"hello"}'
        (chapter / 'lang/lang_en.json').write_bytes(language)
        self.prepare('deltarune')
        common = self.output / 'Assets/butterscotch/common'
        self.assertEqual((common / 'deltarune.win').read_bytes(), data)
        music = (common / 'dr_music.bin').read_bytes()
        self.assertIn(language, music)
        self.assertIn(b'lang_en.json\0', music)
        self.assertEqual(sorted(p.name for p in common.iterdir()), ['deltarune.win', 'dr_music.bin', 'dr_textures.bin'])

    def test_existing_output_is_not_modified(self):
        (self.source / 'data.win').write_bytes(game_data()[0])
        self.output.mkdir()
        save = self.output / 'progress.sav'
        save.write_bytes(b'keep this')
        with self.assertRaises(prep.PreparationError):
            self.prepare()
        self.assertEqual(save.read_bytes(), b'keep this')
        self.assertEqual(list(self.output.iterdir()), [save])

    def test_wrong_bytecode_fails_before_output_is_created(self):
        (self.source / 'data.win').write_bytes(game_data(17)[0])
        with self.assertRaisesRegex(prep.PreparationError, '16'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_incomplete_audio_cannot_be_reported_as_success(self):
        (self.source / 'data.win').write_bytes(game_data(bad_audio=True)[0])
        with self.assertRaisesRegex(prep.PreparationError, 'audio|decode|track'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_source_cannot_be_used_as_output(self):
        (self.source / 'data.win').write_bytes(game_data()[0])
        self.output = self.source / 'generated'
        with self.assertRaises(prep.PreparationError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_corrupt_container_is_rejected(self):
        (self.source / 'data.win').write_bytes(b'FORM' + u32(99999) + b'GEN8')
        with self.assertRaises(prep.PreparationError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_missing_deltarune_audio_chunk_is_rejected(self):
        chapter = self.source / 'chapter1_windows'
        (chapter / 'lang').mkdir(parents=True)
        (chapter / 'data.win').write_bytes(game_data(17, group=1)[0])
        (chapter / 'lang/lang_en.json').write_text('{}')
        (chapter / 'audiogroup1.dat').write_bytes(game_data(17)[0].replace(b'AUDO', b'JUNK'))
        with self.assertRaisesRegex(prep.PreparationError, 'audio|AUDO'):
            self.prepare('deltarune')
        self.assertFalse(self.output.exists())

    def test_embedded_sound_with_original_ogg_name_needs_no_external_copy(self):
        data = bytearray(game_data()[0])
        name = b'embedded.ogg\0'
        data += b'STRG' + u32(len(name)) + name
        struct.pack_into('<I', data, 4, len(data)-8)
        entry = struct.unpack_from('<I', data, data.index(b'SOND')+12)[0]
        struct.pack_into('<I', data, entry+12, len(data)-len(name))
        (self.source / 'data.win').write_bytes(data)
        self.prepare()
        self.assertTrue((self.output / 'Assets/butterscotch/common/music.bin').exists())

    def test_empty_deltarune_audio_bank_is_not_a_complete_install(self):
        chapter = self.source / 'chapter1_windows'
        (chapter / 'lang').mkdir(parents=True)
        (chapter / 'data.win').write_bytes(game_data(17, group=1)[0])
        (chapter / 'lang/lang_en.json').write_text('{}')
        (chapter / 'audiogroup1.dat').write_bytes(b'FORM' + u32(12) + b'AUDO' + u32(4) + u32(0))
        with self.assertRaisesRegex(prep.PreparationError, 'audio|track'):
            self.prepare('deltarune')
        self.assertFalse(self.output.exists())

    def test_missing_referenced_music_is_rejected(self):
        data = bytearray(game_data()[0])
        data += b'STRG' + u32(10) + b'theme.ogg\0'
        struct.pack_into('<I', data, 4, len(data)-8)
        sond = data.index(b'SOND')
        entry = struct.unpack_from('<I', data, sond+12)[0]
        struct.pack_into('<I', data, entry+12, len(data)-10)
        struct.pack_into('<I', data, entry+4, 0x64)
        struct.pack_into('<i', data, entry+32, -1)
        (self.source / 'data.win').write_bytes(data)
        (self.source / 'other.ogg').write_bytes(b'not a real ogg')
        with self.assertRaisesRegex(prep.PreparationError, 'theme'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_release_bundle_cli_works_outside_the_source_checkout(self):
        bundle = self.root / 'preparation bundle 雪'
        native = bundle / 'bin' / prep.host_tag()
        shutil.copytree(self.tools, native)
        script = bundle / 'prepare_game.py'
        shutil.copyfile(Path(prep.__file__), script)
        (self.source / 'data.win').write_bytes(game_data()[0])
        parent = self.root / 'output parent 雪'
        parent.mkdir()
        self.output = parent / 'prepared data'
        result = subprocess.run([sys.executable, str(script), '--game', 'undertale',
                                 '--source', str(self.source), '--output', str(self.output)],
                                cwd=self.root, capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Ready:', result.stdout)
        self.assertTrue((self.output / 'Assets/butterscotch/common/music.bin').exists())

    def test_platform_aliases_and_unknown_hosts(self):
        for system, arch, want in [('Darwin', 'arm64', 'macos-arm64'), ('Darwin', 'x86_64', 'macos-x86_64'),
                                   ('Windows', 'AMD64', 'windows-x86_64'), ('Linux', 'aarch64', 'linux-arm64')]:
            self.assertEqual(prep.host_tag(system, arch), want)
        with self.assertRaises(prep.PreparationError):
            prep.host_tag('unknown', 'unknown')


if __name__ == '__main__':
    unittest.main()
