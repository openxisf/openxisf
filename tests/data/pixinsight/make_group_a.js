// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Writes the samples of group A with PixInsight: the 37 x 23 pattern image, created with PixelMath in each sample
// format and colour space of the group, and saved without compression or checksums. tests/samples/sample_catalog.cpp
// describes the samples. Run it in automation mode, for example on Windows:
//
//   PixInsight.exe -n --automation-mode -r="make_group_a.js,<output directory>" --force-exit
//
// It writes a log, group_a.log, next to the samples.

var outputDirectory = (typeof jsArguments !== "undefined" && jsArguments.length > 0) ?
    jsArguments[0] : File.extractDirectory(#__FILE__);

var log = "";

function note(text)
{
    log += text + "\n";
}

// Channel 0 increases in storage order, channel 1 decreases, channel 2 follows x and the alpha channel follows y.
var red = "(x() + 37*y())/850";
var green = "1 - (x() + 37*y())/850";
var blue = "x()/36";
var alpha = "y()/22";

var samples = [
    { file: "a1-gray-u8.xisf", rgb: false, alpha: false, format: PixelMath.prototype.i8 },
    { file: "a2-gray-u16.xisf", rgb: false, alpha: false, format: PixelMath.prototype.i16 },
    { file: "a3-gray-u32.xisf", rgb: false, alpha: false, format: PixelMath.prototype.i32 },
    { file: "a4-rgb-f32.xisf", rgb: true, alpha: false, format: PixelMath.prototype.f32 },
    { file: "a5-gray-f64.xisf", rgb: false, alpha: false, format: PixelMath.prototype.f64 },
    { file: "a6-rgba-u16.xisf", rgb: true, alpha: true, format: PixelMath.prototype.i16 }
];

var hints = "no-compress-data no-checksums block-alignment 4096 properties fits-keywords";

function writeSample(sample, index)
{
    var id = "openxisf_sample_" + index;
    var P = new PixelMath;
    P.expression = red;
    P.expression1 = green;
    P.expression2 = blue;
    P.expression3 = alpha;
    P.useSingleExpression = !sample.rgb;
    P.rescale = false;
    P.truncate = true;
    P.createNewImage = true;
    P.showNewImage = true;
    P.newImageId = id;
    P.newImageWidth = 37;
    P.newImageHeight = 23;
    P.newImageAlpha = sample.alpha;
    P.newImageColorSpace = sample.rgb ? PixelMath.prototype.RGB : PixelMath.prototype.Gray;
    P.newImageSampleFormat = sample.format;
    if (!P.executeGlobal())
        throw new Error("PixelMath failed for " + sample.file);

    var window = ImageWindow.windowById(id);
    if (window.isNull)
        throw new Error("PixelMath created no image for " + sample.file);
    var path = outputDirectory + "/" + sample.file;
    if (!window.saveAs(path, false, false, false, false, hints))
        throw new Error("cannot save " + path);
    window.forceClose();
    note("wrote " + path);
}

try
{
    if (typeof coreVersionMajor !== "undefined")
        note("PixInsight " + coreVersionMajor + "." + coreVersionMinor + "." + coreVersionRelease + "-" +
             coreVersionRevision + " build " + coreVersionBuild);
    note("format hints: " + hints);
    for (var i = 0; i < samples.length; ++i)
        writeSample(samples[i], i + 1);
    note("done");
}
catch (failure)
{
    note("error: " + failure);
}

File.writeTextFile(outputDirectory + "/group_a.log", log);
