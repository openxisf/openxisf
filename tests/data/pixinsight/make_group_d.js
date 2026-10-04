// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Writes the samples of group D that PixInsight can make:
//
// - D1: the 37 x 23 pattern image in RGB and UInt16 (the three colour channels of group A), saved from its image window
//   with FITS keywords (HISTORY and COMMENT among them, quoted strings, and a string value longer than a FITS card can
//   hold), Observation and Instrument properties, small vectors and a larger one, an RGB working space with the
//   primaries of Adobe RGB (1998), a display function of an STF on each channel, a resolution in pixels per
//   centimetre, and a maximum inline block size of 64 bytes, so that the small vectors are inline blocks and the other
//   blocks are attached. PixInsight adds its sRGB ICC profile and the thumbnail of the global preferences.
// - D2: a synthetic 64 x 48 Bayer frame (Gray, UInt16) whose samples increase in storage order, written through a
//   FileFormatInstance with a ColorFilterArray of the RGGB pattern, the image type Light and the keywords of a camera.
//   It stands for a raw frame opened as a CFA image: what the XISF module writes for one is the same, and a synthetic
//   frame holds no data of a real camera or observation.
// - D3: the 37 x 23 pattern image of sample A2 (Gray, UInt16) with a property of each type that PixInsight writes, set
//   on its main view as storable properties: the extremes of the integer types, a string with spaces at both ends and
//   characters beyond ASCII, a long string, a time point with milliseconds, vectors and matrices, empty ones and a
//   large one. PixInsight scripts cannot give a property a format specifier or a comment, and PixInsight 1.9.4 cannot
//   write a complex scalar (it writes the Property element without a value attribute and fails the save), so
//   constructed units cover those.
// - D4: three images in one unit, written one after the other through a FileFormatInstance: the image of A2 with the
//   id "first", the image of A4 (RGB, Float32) with the id "second", and an 11 x 7 Gray UInt8 image whose samples
//   increase in storage order, with the id "third". Each has one property of its own.
//
// tests/samples/sample_catalog.cpp describes the samples. Run it in automation mode, for example on Windows:
//
//   PixInsight.exe -n --automation-mode -r="make_group_d.js,<output directory>[,<sample>]" --force-exit
//
// where <sample> is D1, D2, D3 or D4 to write only that one. It writes a log, group_d.log, next to the samples.

#include <pjsr/PropertyAttribute.jsh>
#include <pjsr/PropertyType.jsh>

var outputDirectory = (typeof jsArguments !== "undefined" && jsArguments.length > 0) ?
    jsArguments[0] : File.extractDirectory(#__FILE__);
var onlySample = (typeof jsArguments !== "undefined" && jsArguments.length > 1) ? jsArguments[1] : "";

var log = "";

function note(text)
{
    log += text + "\n";
}

// An image created by PixelMath with the expressions of the pattern image (those of group A), one for gray images and
// three for RGB images, without rescaling and with truncation.
function createPattern(id, width, height, expressions, format)
{
    var P = new PixelMath;
    P.expression = expressions[0];
    if (expressions.length > 1)
    {
        P.expression1 = expressions[1];
        P.expression2 = expressions[2];
    }
    P.useSingleExpression = expressions.length == 1;
    P.rescale = false;
    P.truncate = true;
    P.createNewImage = true;
    P.showNewImage = true;
    P.newImageId = id;
    P.newImageWidth = width;
    P.newImageHeight = height;
    P.newImageAlpha = false;
    P.newImageColorSpace = expressions.length == 1 ? PixelMath.prototype.Gray : PixelMath.prototype.RGB;
    P.newImageSampleFormat = format;
    if (!P.executeGlobal())
        throw new Error("PixelMath failed for " + id);
    var window = ImageWindow.windowById(id);
    if (window.isNull)
        throw new Error("PixelMath created no image for " + id);
    return window;
}

// The image of sample A2: channel 0 of the pattern, which increases in storage order.
function createImage(id)
{
    return createPattern(id, 37, 23, ["(x() + 37*y())/850"], PixelMath.prototype.i16);
}

var attributes = PropertyAttribute_Storable | PropertyAttribute_Permanent;

// A property that PixInsight cannot set is reported in the log and left out.
function set(view, id, value, type)
{
    try
    {
        if (!view.setPropertyValue(id, value, type, attributes))
            throw new Error("setPropertyValue() failed");
        note("set " + id + " as type " + type + "; PixInsight reports type " + view.propertyType(id));
    }
    catch (failure)
    {
        note("cannot set " + id + ": " + failure);
    }
}

// The elements of an array in a Vector, which is how PJSR takes the values of real vector properties.
function vector(elements)
{
    var result = new Vector(elements.length);
    for (var i = 0; i < elements.length; ++i)
        result.at(i, elements[i]);
    return result;
}

// [minimum, -1 or 0, 1, maximum] of an element type. JavaScript numbers hold integers exactly up to 2^53 - 1, which
// limits the 64-bit extremes.
function extremes(minimum, maximum)
{
    return vector([minimum, minimum < 0 ? -1 : 0, 1, maximum]);
}

var largestInteger = 9007199254740991; // 2^53 - 1
var largestFloat32 = 3.4028234663852886e38;
var largestFloat64 = 1.7976931348623157e308;

function setProperties(view)
{
    set(view, "Test:Boolean", true, PropertyType_Boolean);
    set(view, "Test:Int8", -128, PropertyType_Int8);
    set(view, "Test:UInt8", 255, PropertyType_UInt8);
    set(view, "Test:Int16", -32768, PropertyType_Int16);
    set(view, "Test:UInt16", 65535, PropertyType_UInt16);
    set(view, "Test:Int32", -2147483648, PropertyType_Int32);
    set(view, "Test:UInt32", 4294967295, PropertyType_UInt32);
    set(view, "Test:Int64", -largestInteger, PropertyType_Int64);
    set(view, "Test:UInt64", largestInteger, PropertyType_UInt64);
    set(view, "Test:Float32", 0.1, PropertyType_Float32);
    set(view, "Test:Float64", 1e-300, PropertyType_Float64);
    // Two spaces, N with tilde, and, u with acute, an em dash, two CJK ideographs, a check mark, two spaces.
    set(view, "Test:String", "  Ñandú — 星雲 ✓  ", PropertyType_String);
    var longString = "";
    for (var i = 0; i < 1000; ++i)
        longString += "0123456789";
    set(view, "Test:LongString", longString, PropertyType_String);
    set(view, "Test:TimePoint", new Date(Date.UTC(2026, 2, 14, 1, 59, 26, 535)), PropertyType_TimePoint);

    set(view, "Test:I8Vector", extremes(-128, 127), PropertyType_I8Vector);
    set(view, "Test:UI8Vector", extremes(0, 255), PropertyType_UI8Vector);
    set(view, "Test:I16Vector", extremes(-32768, 32767), PropertyType_I16Vector);
    set(view, "Test:UI16Vector", extremes(0, 65535), PropertyType_UI16Vector);
    set(view, "Test:I32Vector", extremes(-2147483648, 2147483647), PropertyType_I32Vector);
    set(view, "Test:UI32Vector", extremes(0, 4294967295), PropertyType_UI32Vector);
    set(view, "Test:I64Vector", extremes(-largestInteger, largestInteger), PropertyType_I64Vector);
    set(view, "Test:UI64Vector", extremes(0, largestInteger), PropertyType_UI64Vector);
    set(view, "Test:F32Vector", extremes(-largestFloat32, largestFloat32), PropertyType_F32Vector);
    set(view, "Test:F64Vector", extremes(-largestFloat64, largestFloat64), PropertyType_F64Vector);
    set(view, "Test:C32Vector", [new Complex(1, 2), new Complex(-3, 4)], PropertyType_C32Vector);
    set(view, "Test:C64Vector", [new Complex(1, 2), new Complex(-3, 4)], PropertyType_C64Vector);
    set(view, "Test:EmptyVector", new Vector(0), PropertyType_F64Vector);

    var large = new Vector(100000);
    for (var i = 0; i < 100000; ++i)
        large.at(i, i * 0.5);
    set(view, "Test:LargeVector", large, PropertyType_F32Vector);

    var matrix = new Matrix(3, 4);
    for (var r = 0; r < 3; ++r)
        for (var c = 0; c < 4; ++c)
            matrix.at(r, c, 10 * r + c);
    set(view, "Test:F64Matrix", matrix, PropertyType_F64Matrix);
    var small = new Matrix(2, 2);
    small.at(0, 0, 1);
    small.at(0, 1, 2);
    small.at(1, 0, 3);
    small.at(1, 1, 4);
    set(view, "Test:UI16Matrix", small, PropertyType_UI16Matrix);
    set(view, "Test:EmptyMatrix", new Matrix(0, 0), PropertyType_F64Matrix);
}

var hints = "no-compression no-checksums block-alignment 4096 properties fits-keywords";

// The keywords of D1: a quoted string, a number, both commentary keywords with their text in the comment, a string of
// exactly 68 characters, the most a FITS card holds, and one of 90 characters.
function d1Keywords()
{
    var long68 = "";
    while (long68.length < 68)
        long68 += "0123456789";
    long68 = long68.substring(0, 68);
    var long90 = "";
    for (var i = 0; i < 9; ++i)
        long90 += "abcdefghij";
    return [
        new FITSKeyword("OBJECT", "'M31'", "Name of the object"),
        new FITSKeyword("EXPTIME", "300.", "Exposure time in seconds"),
        new FITSKeyword("HISTORY", "", "Calibrated with a master dark"),
        new FITSKeyword("COMMENT", "", "Written by the OpenXISF sample script"),
        new FITSKeyword("CARD68", "'" + long68 + "'", "The longest string of a FITS card"),
        new FITSKeyword("LONGSTR", "'" + long90 + "'", "Longer than a FITS card")
    ];
}

function setD1Properties(view)
{
    set(view, "Observation:Object:Name", "M31", PropertyType_String);
    set(view, "Observation:Center:RA", 10.684708, PropertyType_Float64);
    set(view, "Observation:Center:Dec", 41.26875, PropertyType_Float64);
    set(view, "Observation:Time:Start", new Date(Date.UTC(2026, 8, 20, 23, 15, 0, 0)), PropertyType_TimePoint);
    set(view, "Instrument:ExposureTime", 300, PropertyType_Float32);
    set(view, "Instrument:Camera:Name", "Synthetic camera", PropertyType_String);
    set(view, "Instrument:Telescope:FocalLength", 0.53, PropertyType_Float32);
    set(view, "Instrument:Sensor:Temperature", -10, PropertyType_Float32);
    // 32 bytes each, inline at a maximum inline block size of 64 bytes; the vector of 800 bytes is attached.
    set(view, "Test:SmallVector", vector([1, 2, 3, 4]), PropertyType_F64Vector);
    set(view, "Test:SmallMatrix", (function () {
        var m = new Matrix(2, 2);
        m.at(0, 0, 1);
        m.at(0, 1, 2);
        m.at(1, 0, 3);
        m.at(1, 1, 4);
        return m;
    })(), PropertyType_F64Matrix);
    var large = new Vector(100);
    for (var i = 0; i < 100; ++i)
        large.at(i, i * 0.25);
    set(view, "Test:AttachedVector", large, PropertyType_F64Vector);
}

function writeD1()
{
    var window = createPattern("openxisf_sample_d1", 37, 23,
                               ["(x() + 37*y())/850", "1 - (x() + 37*y())/850", "x()/36"], PixelMath.prototype.i16);
    window.keywords = d1Keywords();
    // Adobe RGB (1998) relative to D50, as in the examples of the specification: gamma, sRGB gamma function, then the
    // luminance coefficients and the x and y chromaticities. PixInsight holds them as 32-bit floating point values.
    window.rgbWorkingSpace = new RGBColorSystem(2.2, false, [0.311114, 0.625662, 0.063224],
                                                [0.648431, 0.230154, 0.155886], [0.330856, 0.701572, 0.066044]);
    // One STF for each colour channel, and one for the lightness: [m, s, h, l, r].
    window.mainView.stf = [[0.25, 0.01, 0.9, 0, 1], [0.3, 0.02, 0.95, 0, 1], [0.35, 0.03, 1, 0, 1],
                           [0.5, 0, 1, 0, 1]];
    window.metricResolution = true;
    window.horizontalResolution = 120;
    window.verticalResolution = 100;
    setD1Properties(window.mainView);
    var d1Hints = "no-compression no-checksums block-alignment 4096 max-inline-block-size 64 properties fits-keywords";
    var path = outputDirectory + "/d1-rich-rgb.xisf";
    if (!window.saveAs(path, false, false, false, false, d1Hints))
        throw new Error("cannot save " + path);
    note("wrote " + path + " with the format hints '" + d1Hints + "'");
    window.forceClose();
}

// The keywords of the camera that wrote a raw frame, set by hand.
function d2Keywords()
{
    return [
        new FITSKeyword("INSTRUME", "'Synthetic CFA camera'", "Camera"),
        new FITSKeyword("BAYERPAT", "'RGGB'", "Bayer color pattern"),
        new FITSKeyword("XBAYROFF", "0", "X offset of the Bayer pattern"),
        new FITSKeyword("YBAYROFF", "0", "Y offset of the Bayer pattern"),
        new FITSKeyword("EXPTIME", "120.", "Exposure time in seconds"),
        new FITSKeyword("GAIN", "120", "Sensor gain"),
        new FITSKeyword("XPIXSZ", "2.4", "Pixel width in microns"),
        new FITSKeyword("YPIXSZ", "2.4", "Pixel height in microns"),
        new FITSKeyword("CCD-TEMP", "-10.", "Sensor temperature in degrees C")
    ];
}

function writeD2()
{
    var window = createPattern("openxisf_sample_d2", 64, 48, ["(x() + 64*y())/3071"], PixelMath.prototype.i16);
    var format = new FileFormat("XISF", false, true);
    if (format.isNull)
        throw new Error("PixInsight has no XISF format to write with");
    var file = new FileFormatInstance(format);
    var path = outputDirectory + "/d2-cfa-raw.xisf";
    if (!file.create(path, hints))
        throw new Error("cannot create " + path);
    var options = new ImageDescription;
    options.bitsPerSample = 16;
    options.ieeefpSampleFormat = false;
    options.imageType = 4; // Light
    if (!file.setOptions(options))
        throw new Error("cannot describe the image of D2");
    // [pattern, width, height, name]; PixInsight writes it, though reading the property back gives empty values.
    file.colorFilterArray = ["RGGB", 2, 2, "RGGB Bayer filter"];
    file.keywords = d2Keywords();
    if (!file.writeImage(window.mainView.image))
        throw new Error("cannot write the image of D2");
    file.close();
    window.forceClose();
    note("wrote " + path + " with the format hints '" + hints + "'");
}

function writeD3()
{
    var window = createImage("openxisf_sample_d3");
    setProperties(window.mainView);
    var path = outputDirectory + "/d3-typed-properties.xisf";
    if (!window.saveAs(path, false, false, false, false, hints))
        throw new Error("cannot save " + path);
    note("wrote " + path + " with the format hints '" + hints + "'");
    window.forceClose();
}

// The images of D4, in the order they are written, with the property of each.
var d4Images = [
    { id: "first", width: 37, height: 23, expressions: ["(x() + 37*y())/850"], format: PixelMath.prototype.i16,
      bits: 16, real: false, property: "Test:First", value: 1, type: PropertyType_UInt16 },
    { id: "second", width: 37, height: 23,
      expressions: ["(x() + 37*y())/850", "1 - (x() + 37*y())/850", "x()/36"], format: PixelMath.prototype.f32,
      bits: 32, real: true, property: "Test:Second", value: "second image", type: PropertyType_String },
    { id: "third", width: 11, height: 7, expressions: ["(x() + 11*y())/76"], format: PixelMath.prototype.i8,
      bits: 8, real: false, property: "Test:Third", value: 3.5, type: PropertyType_Float64 }
];

function writeD4()
{
    var format = new FileFormat("XISF", false, true);
    if (format.isNull)
        throw new Error("PixInsight has no XISF format to write with");
    var file = new FileFormatInstance(format);
    var path = outputDirectory + "/d4-multi-image.xisf";
    if (!file.create(path, hints, d4Images.length))
        throw new Error("cannot create " + path);
    for (var i = 0; i < d4Images.length; ++i)
    {
        var d = d4Images[i];
        var window = createPattern("openxisf_sample_d4_" + d.id, d.width, d.height, d.expressions, d.format);
        var options = new ImageDescription;
        options.bitsPerSample = d.bits;
        options.ieeefpSampleFormat = d.real;
        if (!file.setOptions(options) || !file.setImageId(d.id))
            throw new Error("cannot describe image " + d.id);
        if (!file.writeImageProperty(d.property, d.value, d.type))
            throw new Error("cannot write the property of image " + d.id);
        if (!file.writeImage(window.mainView.image))
            throw new Error("cannot write image " + d.id);
        window.forceClose();
        note("wrote image " + d.id);
    }
    file.close();
    note("wrote " + path + " with the format hints '" + hints + "'");
}

try
{
    if (typeof coreVersionMajor !== "undefined")
        note("PixInsight " + coreVersionMajor + "." + coreVersionMinor + "." + coreVersionRelease + "-" +
             coreVersionRevision + " build " + coreVersionBuild);
    if (onlySample == "" || onlySample == "D1")
        writeD1();
    if (onlySample == "" || onlySample == "D2")
        writeD2();
    if (onlySample == "" || onlySample == "D3")
        writeD3();
    if (onlySample == "" || onlySample == "D4")
        writeD4();
    note("done");
}
catch (failure)
{
    note("error: " + failure);
}

File.writeTextFile(outputDirectory + "/group_d.log", log);
