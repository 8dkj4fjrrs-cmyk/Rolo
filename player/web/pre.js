// Rolo's web player: what happens in the page before the game starts.
// (Built into rolo_player.js with Emscripten's --pre-js; see
// player/CMakeLists.txt and docs/formats/game-pack.md.)
//
// 1. The game's files arrive as one file, game.pack, beside the page. They
//    are unpacked into the player's in-memory file system under /game,
//    where the player finds the project as it does on a desktop.
// 2. Saved games are kept by the browser (IndexedDB), in a folder named
//    after the game, and read back before the game starts.
//
// The page (index.html) may set, on Module:
//   roloGamePack     the pack's address (default "game.pack")
//   onRoloProgress   function(loadedBytes, totalBytes or 0)
//   onRoloError      function(message): the game could not start
//   onRoloGameEnded  function(): the game quit

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  var MAGIC = 'ROLOPACK1\n';

  function fail(message) {
    console.error('Rolo: ' + message);
    if (Module['onRoloError']) Module['onRoloError'](message);
  }

  // Makes the folders of `path` (which ends in a file name).
  function makeFoldersFor(path) {
    var parts = path.split('/');
    var at = '';
    for (var i = 1; i < parts.length - 1; i++) {
      at += '/' + parts[i];
      try { FS.mkdir(at); } catch (e) { /* it is there already */ }
    }
  }

  function unpack(buffer) {
    var bytes = new Uint8Array(buffer);
    var magic = '';
    for (var i = 0; i < MAGIC.length && i < bytes.length; i++) magic += String.fromCharCode(bytes[i]);
    if (magic !== MAGIC) throw new Error('game.pack is not a Rolo game pack (or is damaged).');
    var view = new DataView(buffer);
    var indexLength = view.getUint32(MAGIC.length, true);
    var indexStart = MAGIC.length + 4;
    var index = JSON.parse(new TextDecoder('utf-8').decode(bytes.subarray(indexStart, indexStart + indexLength)));
    var at = indexStart + indexLength;
    for (var f = 0; f < index.files.length; f++) {
      var file = index.files[f];
      // Paths are checked when packing; refuse anything that could leave /game anyway.
      if (file.path.indexOf('..') !== -1 || file.path.charAt(0) === '/') throw new Error('game.pack has a bad path.');
      if (at + file.size > bytes.length) throw new Error('game.pack is cut short (was it fully uploaded?).');
      var path = '/game/' + file.path;
      makeFoldersFor(path);
      FS.writeFile(path, bytes.subarray(at, at + file.size));
      at += file.size;
    }
    return index;
  }

  // Reads the whole pack, reporting progress.
  function download(url) {
    return fetch(url).then(function (response) {
      if (!response.ok) throw new Error('Could not load ' + url + ' (the server answered ' + response.status + ').');
      var total = parseInt(response.headers.get('Content-Length') || '0', 10);
      if (!response.body || !response.body.getReader) return response.arrayBuffer();
      var reader = response.body.getReader();
      var chunks = [];
      var loaded = 0;
      function pump() {
        return reader.read().then(function (step) {
          if (step.done) {
            var all = new Uint8Array(loaded);
            var offset = 0;
            for (var c = 0; c < chunks.length; c++) { all.set(chunks[c], offset); offset += chunks[c].length; }
            return all.buffer;
          }
          chunks.push(step.value);
          loaded += step.value.length;
          if (Module['onRoloProgress']) Module['onRoloProgress'](loaded, total);
          return pump();
        });
      }
      return pump();
    });
  }

  addRunDependency('rolo-game');
  download(Module['roloGamePack'] || 'game.pack').then(function (buffer) {
    var index = unpack(buffer);
    // Saves: one folder per game, so games on the same site keep apart.
    var saves = '/saves/' + (index.saves || 'game');
    try { FS.mkdir('/saves'); } catch (e) { /* there already */ }
    FS.mkdir(saves);
    FS.mount(IDBFS, {}, saves);
    // The player's command line. (Emscripten has read Module.arguments by
    // now, so its own copy, arguments_, is set as well.)
    var args = ['--project', '/game', '--save-dir', saves, '--no-stats'].concat(Module['arguments'] || []);
    Module['arguments'] = args;
    if (typeof arguments_ !== 'undefined') arguments_ = args;
    FS.syncfs(true, function (error) {
      if (error) console.warn('Rolo: saved games could not be read; they will not be kept. ' + error);
      removeRunDependency('rolo-game');
    });
  }).catch(function (error) {
    fail(error && error.message ? error.message : String(error));
  });
});

// Called by the player after a save is written: hands it to the browser to keep.
Module['roloKeepSaves'] = function () {
  FS.syncfs(false, function (error) {
    if (error) console.warn('Rolo: the saved game could not be kept by the browser. ' + error);
  });
};
