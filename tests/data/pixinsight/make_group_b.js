// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Writes the samples of group B with PixInsight: the pattern images of samples A4 (RGB, Float32), A2 (Gray, UInt16) and
// A5 (Gray, Float64), created with PixelMath as in make_group_a.js and saved with each compression codec at its default
// level. tests/samples/sample_catalog.cpp describes the samples. Run it in automation mode, for example on Windows:
//
//   PixInsight.exe -n --automation-mode -r="make_group_b.js,<output directory>" --force-exit
//
// It writes a log, group_b.log, next to the samples.

var outputDirectory = (typeof jsArguments !== "undefined" && jsArguments.length > 0) ?
    jsArguments[0] : File.extractDirectory(#__FILE__);

var log = "";

function note(text)
{
    log += text + "\n";
}

// Channel 0 increases in storage order, channel 1 decreases and channel 2 follows x.
var red = "(x() + 37*y())/850";
var green = "1 - (x() + 37*y())/850";
var blue = "x()/36";

var commonHints = "no-checksums block-alignment 4096 properties fits-keywords";

var images = {
    a4: { rgb: true, format: PixelMath.prototype.f32 },
    a2: { rgb: false, format: PixelMath.prototype.i16 },
    a5: { rgb: false, format: PixelMath.prototype.f64 }
};

var samples = [
    { file: "b1-rgb-f32-zlib.xisf", image: "a4", codec: "zlib" },
    { file: "b2-rgb-f32-zlib-sh.xisf", image: "a4", codec: "zlib+sh" },
    { file: "b3-rgb-f32-lz4.xisf", image: "a4", codec: "lz4" },
    { file: "b4-rgb-f32-lz4-sh.xisf", image: "a4", codec: "lz4+sh" },
    { file: "b5-rgb-f32-lz4hc.xisf", image: "a4", codec: "lz4hc" },
    { file: "b6-rgb-f32-lz4hc-sh.xisf", image: "a4", codec: "lz4hc+sh" },
    { file: "b7-rgb-f32-zstd.xisf", image: "a4", codec: "zstd" },
    { file: "b8-rgb-f32-zstd-sh.xisf", image: "a4", codec: "zstd+sh" },
    { file: "b9-gray-u16-zstd-sh.xisf", image: "a2", codec: "zstd+sh" },
    { file: "b10-gray-f64-zlib-sh.xisf", image: "a5", codec: "zlib+sh" }
];

function createImage(id, image)
{
    var P = new PixelMath;
    P.expression = red;
    P.expression1 = green;
    P.expression2 = blue;
    P.useSingleExpression = !image.rgb;
    P.rescale = false;
    P.truncate = true;
    P.createNewImage = true;
    P.showNewImage = true;
    P.newImageId = id;
    P.newImageWidth = 37;
    P.newImageHeight = 23;
    P.newImageAlpha = false;
    P.newImageColorSpace = image.rgb ? PixelMath.prototype.RGB : PixelMath.prototype.Gray;
    P.newImageSampleFormat = image.format;
    if (!P.executeGlobal())
        throw new Error("PixelMath failed for " + id);
    var window = ImageWindow.windowById(id);
    if (window.isNull)
        throw new Error("PixelMath created no image for " + id);
    return window;
}

try
{
    if (typeof coreVersionMajor !== "undefined")
        note("PixInsight " + coreVersionMajor + "." + coreVersionMinor + "." + coreVersionRelease + "-" +
             coreVersionRevision + " build " + coreVersionBuild);
    // One image of each kind, so that every sample made from it holds the same pixels.
    var windows = {};
    for (var key in images)
        windows[key] = createImage("openxisf_sample_" + key, images[key]);
    for (var i = 0; i < samples.length; ++i)
    {
        var path = outputDirectory + "/" + samples[i].file;
        var hints = "compression-codec " + samples[i].codec + " " + commonHints;
        if (!windows[samples[i].image].saveAs(path, false, false, false, false, hints))
            throw new Error("cannot save " + path);
        note("wrote " + path + " with the format hints '" + hints + "'");
    }
    for (var key in windows)
        windows[key].forceClose();
    note("done");
}
catch (failure)
{
    note("error: " + failure);
}

File.writeTextFile(outputDirectory + "/group_b.log", log);
