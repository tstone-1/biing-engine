"""Startup orchestration uses synthetic inputs; the manual check builds real clean clones."""
from pathlib import Path
from types import SimpleNamespace
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import play


def test_paths_from_drag_and_drop_are_not_shell_commands(tmp_path):
    folder = tmp_path / 'A game!'
    folder.mkdir()
    assert play.source_path(str(folder)) == folder
    assert play.source_path('"' + str(folder) + '"') == folder
    if play.os.name != 'nt':
        assert play.source_path(str(folder).replace(' ', '\\ ')) == folder
    assert play.source_path('$(touch SENTINEL)') == Path('$(touch SENTINEL)')
    assert not Path('SENTINEL').exists()
    with pytest.raises(play.SetupError, match='no game source'):
        play.source_path('  ')


@pytest.fixture
def startup(tmp_path, monkeypatch):
    repo = tmp_path / 'repo'
    repo.mkdir()
    source = tmp_path / 'original.zip'
    source.write_bytes(b'synthetic original')
    game = tmp_path / 'game'
    commands = []
    monkeypatch.setattr(play.shutil, 'which', lambda name: '/synthetic/' + name)
    monkeypatch.setattr(play, 'identify', lambda path: SimpleNamespace(fix=''))

    def setup(src, dest):
        assert src == source
        assert src.read_bytes() == b'synthetic original'
        dest.mkdir()
        (dest / 'BIPRO.EXE').write_bytes(b'synthetic program')
        (dest / 'GAMES').mkdir()
        return '[OK] synthetic prepared'

    def run(command, **kwargs):
        commands.append(command)
        if Path(command[0]).name.startswith('biing-check'):
            assert (game / 'BIPRO.EXE').exists()
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(play, 'setup', setup)
    monkeypatch.setattr(play.subprocess, 'run', run)
    return repo, source, game, commands


def test_first_run_and_reuse_preserve_sources_and_saves(startup, monkeypatch):
    repo, source, game, commands = startup
    monkeypatch.setattr('builtins.input', lambda prompt: str(source))
    engine = play.prepare(repo, game)
    assert engine.parent == repo / 'build'
    assert len(commands) == 3 and Path(commands[-1][0]).name.startswith('biing-check')
    save = game / 'GAMES' / 'BGAME1'
    save.write_bytes(b'synthetic saved progress')
    monkeypatch.setattr('builtins.input', lambda prompt: pytest.fail('Existing games must not ask for a source'))
    monkeypatch.setattr(play, 'setup', lambda *args: pytest.fail('Existing games must not be re-created'))
    play.prepare(repo, game)
    assert save.read_bytes() == b'synthetic saved progress'
    assert source.read_bytes() == b'synthetic original'
    with pytest.raises(play.SetupError, match='already contains'):
        play.prepare(repo, game, source)


def test_failed_build_does_not_copy_or_validate_game(startup, monkeypatch):
    repo, source, game, commands = startup
    monkeypatch.setattr(play.subprocess, 'run', lambda *a, **k: SimpleNamespace(returncode=1))
    monkeypatch.setattr(play, 'setup', lambda *args: pytest.fail('No setup after a failed build'))
    with pytest.raises(play.SetupError, match='build failed'):
        play.prepare(repo, game, source)
    assert not game.exists() and source.read_bytes() == b'synthetic original'


def test_failed_asset_check_does_not_launch_or_remove_game(startup, monkeypatch):
    repo, source, game, commands = startup
    play.prepare(repo, game, source)
    (game / 'GAMES' / 'BGAME1').write_bytes(b'keep me')
    monkeypatch.setattr(play.subprocess, 'run', lambda command, **kwargs: SimpleNamespace(returncode=int(Path(command[0]).name.startswith('biing-check'))))
    with pytest.raises(play.SetupError, match='game-file check failed'):
        play.prepare(repo, game)
    assert (game / 'GAMES' / 'BGAME1').read_bytes() == b'keep me'


def test_prepare_only_and_normal_launch_use_the_same_preparation(monkeypatch, tmp_path):
    calls = []
    engine = tmp_path / 'engine'
    game = tmp_path / 'game'
    monkeypatch.setattr(play, 'prepare', lambda *args: (calls.append(args), engine)[1])
    monkeypatch.setattr(play.subprocess, 'call', lambda args: (calls.append(args), 7)[1])
    assert play.main(['--game', str(game), '--prepare-only']) == 0
    assert len(calls) == 1
    assert play.main(['--game', str(game), '--no-intro', '--fixes']) == 7
    assert calls[0] == calls[1]
    assert calls[2] == [str(engine), str(game), '--no-intro', '--fixes']
