// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Writes the optional large sample L1 with PixInsight: an RGB Float32 image of 22000 x 17000 pixels (4.18 GiB), above
// the 4 GiB that zlib compresses at once, saved with zlib, so that PixInsight divides it into subblocks of its own
// choice. It is not versioned: tests find it through the OPENXISF_LARGE_SAMPLES_DIR environment variable and skip
// without it. tests/samples/sample_catalog.cpp describes it. Run it in automation mode, for example on Windows:
//
//   PixInsight.exe -n --automation-mode -r="make_large_l1.js,<output directory>" --force-exit
//
// It writes a log, large_l1.log, next to the sample.

var outputDirectory = (typeof jsArguments !== "undefined" && jsArguments.length > 0) ?
    jsArguments[0] : File.extractDirectory(#__FILE__);

var log = "";

function note(text)
{
    log += text + "\n";
}

var width = 22000;
var height = 17000;
var hints = "compression-codec zlib no-checksums block-alignment 4096 properties fits-keywords";

try
{
    if (typeof coreVersionMajor !== "undefined")
        note("PixInsight " + coreVersionMajor + "." + coreVersionMinor + "." + coreVersionRelease + "-" +
             coreVersionRevision + " build " + coreVersionBuild);
    var P = new PixelMath;
    // Channel 0 follows x, channel 1 follows y, and channel 2 both.
    P.expression = "x()/" + (width - 1);
    P.expression1 = "y()/" + (height - 1);
    P.expression2 = "(x() + y())/" + (width + height - 2);
    P.useSingleExpression = false;
    P.rescale = false;
    P.truncate = true;
    P.createNewImage = true;
    P.showNewImage = true;
    P.newImageId = "openxisf_sample_l1";
    P.newImageWidth = width;
    P.newImageHeight = height;
    P.newImageAlpha = false;
    P.newImageColorSpace = PixelMath.prototype.RGB;
    P.newImageSampleFormat = PixelMath.prototype.f32;
    if (!P.executeGlobal())
        throw new Error("PixelMath failed");
    var window = ImageWindow.windowById("openxisf_sample_l1");
    if (window.isNull)
        throw new Error("PixelMath created no image");
    var path = outputDirectory + "/l1-rgb-f32-zlib.xisf";
    if (!window.saveAs(path, false, false, false, false, hints))
        throw new Error("cannot save " + path);
    window.forceClose();
    note("wrote " + path + " with the format hints '" + hints + "'");
    note("done");
}
catch (failure)
{
    note("error: " + failure);
}

File.writeTextFile(outputDirectory + "/large_l1.log", log);
