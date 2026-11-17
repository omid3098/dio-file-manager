# The film

The 14.5-second film at the top of the main README (`docs/media/film.mp4`), made with HyperFrames from `index.html`.
Before a render, copy `SegUIVar.ttf` from `C:\Windows\Fonts\` into `assets\fonts\`. It is Microsoft's font and is not in this repository.
Then, in this folder:

    npm install hyperframes@0.9.58
    npx hyperframes render -f 60 -o out\film.mp4

The WebP in `docs/media/film.webp` is that MP4 at 30 frames per second, 1280 px wide.
