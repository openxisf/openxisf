// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
//
// Writes the samples of astrometric solutions (spec §11.5.3.7) and PixInsight's own evaluation of them:
//
// - d5-astrometry.xisf: a 400 x 300 Gray UInt16 image with a solution as ImageSolver 6.5.0 writes one with its
//   default distortion correction: the projection, the projective transformations and the control points of the
//   standard properties, PixInsight's generation parameters (thin plate splines, engine DDM), and the distortion model
//   that the core generates from them with ImageWindow.regenerateAstrometricSolution(), a Global term in each
//   direction. Saved with zstd+sh.
// - d5-astrometry-local.xisf: the same with recursive surface splines of order 4 (VariableOrder), whose distortion
//   models have Local terms and a Fallback term. The patches of the recursive engine are smaller than ImageSolver's,
//   so that a few hundred control points make several Local terms and the unit stays small.
// - d5-astrometry-multiquadric.xisf: the same with multiquadric splines (engine DDM), whose basis function has a shape
//   parameter, and whose Y component has nodes of its own in one direction.
// - d5-reference.csv: PixInsight's coordinates for the three units: image to celestial at the corners, the centre, 20
//   scattered points and 3 points beyond the image, and celestial to image at 5 sky positions.
// - d5-projections.csv: PixInsight's coordinates for linear solutions (the first layer only) of the seven projections
//   of the specification, at 8 reference points each, with the default native coordinates.
//
// The control points are synthetic, so that the samples hold no data of a real observation: random image points of a
// known model, a Gnomonic projection about M42 with a radial distortion and a small wave, and their celestial
// coordinates. PixInsight fits the distortion models to them as it does to the stars that ImageSolver matches.
//
// PixInsight 1.9.5 evaluates solutions through grids of the transformations that it caches over the image, and over
// the region of the projection plane that the image covers, and evaluates the model exactly elsewhere. The grids differ
// from the model by up to 1e-3 pixels, so the reference coordinates are evaluated on a copy of the solution on a 2 x 2
// image, whose grids cover no point evaluated: the corner at the origin is evaluated at (-1e-9, -1e-9).
//
// Run it in automation mode, for example on Windows:
//
//   PixInsight.exe -n --automation-mode -r="make_group_d5.js,<output directory>" --force-exit
//
// It writes a log, group_d5.log, next to the samples.

#engine v8

var outputDirectory = (typeof jsArguments !== "undefined" && jsArguments.length > 0) ?
    jsArguments[0] : File.extractDirectory(#__FILE__);

var log = "";

function note(text)
{
    log += text + "\n";
}

function num(x)
{
    return x.toPrecision(17);
}

var R = 180 / Math.PI;

function sind(a)
{
    return Math.sin(a / R);
}

function cosd(a)
{
    return Math.cos(a / R);
}

function arg(x, y)
{
    return Math.atan2(y, x) * R;
}

// The Gnomonic deprojection of (u, v) about (a0, d0), equation [40] of the specification.
function deproject(a0, d0, u, v)
{
    var D = Math.atan(Math.sqrt(u * u + v * v) / R) * R;
    var b = arg(-v, u);
    var a = a0 + arg(sind(d0) * sind(D) * cosd(b) + cosd(d0) * cosd(D), sind(D) * sind(b));
    var d = Math.asin(sind(d0) * cosd(D) - cosd(d0) * sind(D) * cosd(b)) * R;
    a = a % 360;
    if (a < 0)
        a += 360;
    return [a, d];
}

// A deterministic generator of numbers in [0, 1).
var seed = 12345;

function random()
{
    seed = (seed * 1103515245 + 12345) % 2147483648;
    return seed / 2147483648;
}

// The true model of the control points.
var width = 400, height = 300;
var a0 = 83.82208, d0 = -5.39111; // M42
var x0 = 200, y0 = 150;
var scale = 0.01; // degrees per pixel
var rotation = 12 / R;
var M = [[-scale * Math.cos(rotation), scale * Math.sin(rotation)],
         [-scale * Math.sin(rotation), -scale * Math.cos(rotation)]];

// Radial distortion about an optical centre away from the centre of the image, and a wave of a few tenths of a pixel.
function undistort(x, y)
{
    var xc = 205, yc = 145;
    var dx = (x - xc) / 250, dy = (y - yc) / 250;
    var r2 = dx * dx + dy * dy;
    var f = 1 + 0.003 * r2 - 0.0008 * r2 * r2;
    var w = 0.15 * Math.sin(x / 45) * Math.cos(y / 35);
    return [xc + (x - xc) * f + w, yc + (y - yc) * f - 0.5 * w];
}

function plane(x, y)
{
    var p = undistort(x, y);
    var px = p[0] - x0, py = p[1] - y0;
    return [M[0][0] * px + M[0][1] * py, M[1][0] * px + M[1][1] * py];
}

function setProperty(view, id, value, type)
{
    if (!view.setPropertyValue(id, value, type, PropertyAttribute.Storable | PropertyAttribute.Permanent))
        throw new Error("cannot set " + id);
}

function makeWindow(id, w, h)
{
    var P = new PixelMath;
    P.expression = "(x() + y())/1398";
    P.useSingleExpression = true;
    P.rescale = false;
    P.truncate = true;
    P.createNewImage = true;
    P.showNewImage = true;
    P.newImageId = id;
    P.newImageWidth = w;
    P.newImageHeight = h;
    P.newImageAlpha = false;
    P.newImageColorSpace = PixelMath.Gray;
    P.newImageSampleFormat = PixelMath.i16;
    if (!P.executeGlobal())
        throw new Error("PixelMath failed for " + id);
    var window = ImageWindow.windowById(id);
    if (window.isNull)
        throw new Error("PixelMath created no image for " + id);
    return window;
}

function creatorApplication()
{
    var name = format("PixInsight %d.%d.%d", CoreApplication.versionMajor, CoreApplication.versionMinor,
                      CoreApplication.versionRelease);
    if (CoreApplication.versionRevision != 0)
        name += format("-%d", CoreApplication.versionRevision);
    return name;
}

// The properties that ImageSolver 6.5.0 sets before it asks the core to generate the distortion models.
function addSolution(window, sample)
{
    var view = window.mainView;
    var n = sample.points;
    var pI = [], pG = [], cI = new Matrix(n, 2), cC = new Matrix(n, 2);
    for (var i = 0; i < n; ++i)
    {
        var x = random() * width, y = random() * height;
        var g = plane(x, y);
        var c = deproject(a0, d0, g[0], g[1]);
        pI.push(new Point(x, y));
        pG.push(new Point(g[0], g[1]));
        cI[i][0] = x;
        cI[i][1] = y;
        cC[i][0] = c[0];
        cC[i][1] = c[1];
    }
    var H = Math.homography(pI, pG);
    var L = new Matrix(2, 2);
    L[0][0] = H[0][0];
    L[0][1] = H[0][1];
    L[1][0] = H[1][0];
    L[1][1] = H[1][1];
    var p0 = H.inverse().apply(new Point(0, 0));

    view.beginProcess(UndoFlag.Keywords | UndoFlag.AstrometricSolution);
    setProperty(view, "AstrometricSolution:Version", "1.0", PropertyType.String);
    setProperty(view, "AstrometricSolution:ProjectionSystem", "Gnomonic", PropertyType.IsoString);
    setProperty(view, "AstrometricSolution:CelestialReferenceSystem", "ICRS", PropertyType.IsoString);
    setProperty(view, "AstrometricSolution:ReferenceCelestialCoordinates", new Vector([a0, d0]), PropertyType.F64Vector);
    setProperty(view, "AstrometricSolution:ReferenceImageCoordinates", new Vector([p0.x, p0.y]),
                PropertyType.F64Vector);
    setProperty(view, "AstrometricSolution:LinearTransformationMatrix", L, PropertyType.F64Matrix);
    setProperty(view, "AstrometricSolution:ReferenceNativeCoordinates", new Vector([0, 90]), PropertyType.F64Vector);
    setProperty(view, "AstrometricSolution:CelestialPoleNativeCoordinates", new Vector([180, 90]),
                PropertyType.F64Vector);
    setProperty(view, "AstrometricSolution:CreationTime", new Date(Date.UTC(2026, 9, 5, 12, 0, 0, 0)),
                PropertyType.TimePoint);
    setProperty(view, "AstrometricSolution:CreatorApplication", creatorApplication(), PropertyType.String);
    setProperty(view, "AstrometricSolution:CreatorModule", "make_group_d5.js", PropertyType.String);
    setProperty(view, "AstrometricSolution:CreatorOS", CoreApplication.platform, PropertyType.String);
    setProperty(view, "AstrometricSolution:ProjectiveTransformation:ImageToProjection", Math.homography(pI, pG),
                PropertyType.F64Matrix);
    setProperty(view, "AstrometricSolution:ProjectiveTransformation:ProjectionToImage", Math.homography(pG, pI),
                PropertyType.F64Matrix);
    var g = "PCL:AstrometricSolution:Generation:";
    setProperty(view, g + "Engine", sample.engine, PropertyType.String);
    setProperty(view, g + "RBFType", sample.rbf, PropertyType.String);
    setProperty(view, g + "SplineOrder", sample.order, PropertyType.Int32);
    setProperty(view, g + "SplineSmoothness", 0.005, PropertyType.Float32);
    setProperty(view, g + "MaxSplinePoints", 4000, PropertyType.Int32);
    setProperty(view, g + "UseSimplifiers", true, PropertyType.Boolean);
    setProperty(view, g + "SimplifierRejectFraction", 0.10, PropertyType.Float32);
    if (sample.engine == "Recursive")
    {
        setProperty(view, g + "Recursive:MaxSplineLength", 300, PropertyType.Int32);
        setProperty(view, g + "Recursive:BucketCapacity", 64, PropertyType.Int32);
        setProperty(view, g + "Recursive:CoarsePoints", 250, PropertyType.Int32);
        setProperty(view, g + "Recursive:RadiusFactor", 1.5, PropertyType.Float32);
    }
    setProperty(view, "AstrometricSolution:ControlPoints:Image", cI, PropertyType.F64Matrix);
    setProperty(view, "AstrometricSolution:ControlPoints:Celestial", cC, PropertyType.F64Matrix);
    view.endProcess();
    window.regenerateAstrometricSolution();
    if (!window.hasAstrometricSolution)
        throw new Error("PixInsight generated no solution for " + sample.file);
}

function describe(view, id)
{
    var value = view.propertyValue(id);
    if (value instanceof Matrix)
        return id + " " + value.rows + " x " + value.columns;
    if (value instanceof Vector)
        return id + " " + value.length;
    return id + " " + JSON.stringify(String(value));
}

// The points of the image to evaluate, none of them in the region of the image of the copy, [0, 2] x [0, 2].
function imagePoints()
{
    var points = [[-1e-9, -1e-9], [width, 0], [0, height], [width, height], [width / 2, height / 2]];
    seed = 2026;
    while (points.length < 25)
    {
        var x = Math.round(random() * width * 1000) / 1000, y = Math.round(random() * height * 1000) / 1000;
        if (x > 2 || y > 2)
            points.push([x, y]);
    }
    points.push([-30, 155], [435, -20], [200, 340]);
    return points;
}

// The sky positions to evaluate: those of five image points, rounded to a millionth of a degree.
var skyPoints = [[200, 150], [65, 235], [345, 60], [305, 280], [110, 45]];

var csv = "";

// Copies the standard properties of a solution to a 2 x 2 image, opens the copy and evaluates it.
function evaluate(window, file)
{
    var from = window.mainView;
    var copy = makeWindow("openxisf_d5_copy", 2, 2);
    var to = copy.mainView;
    var ids = from.properties;
    to.beginProcess(UndoFlag.NoSwapFile);
    for (var i = 0; i < ids.length; ++i)
        if (ids[i].startsWith("AstrometricSolution:"))
            setProperty(to, ids[i], from.propertyValue(ids[i]), from.propertyType(ids[i]));
    to.endProcess();
    var path = outputDirectory + "/d5-evaluation-copy.xisf";
    if (!copy.saveAs(path, false, false, false, false, "compression-codec zstd+sh"))
        throw new Error("cannot save " + path);
    copy.forceClose();
    var opened = ImageWindow.open(path)[0];
    if (!opened.hasAstrometricSolution)
        throw new Error("the copy of the solution of " + file + " has no solution");

    // Beyond the grids of both images, the copy evaluates as the sample does.
    var outside = [[-20, -15], [425, 320]];
    for (var i = 0; i < outside.length; ++i)
    {
        var a = window.imageToCelestial(outside[i][0], outside[i][1]);
        var b = opened.imageToCelestial(outside[i][0], outside[i][1]);
        if (a.x != b.x || a.y != b.y)
            throw new Error("the copy of " + file + " evaluates differently from it");
    }

    var points = imagePoints();
    for (var i = 0; i < points.length; ++i)
    {
        var c = opened.imageToCelestial(points[i][0], points[i][1]);
        csv += "image," + file + "," + num(points[i][0]) + "," + num(points[i][1]) + "," +
               (c ? num(c.x) + "," + num(c.y) : ",") + "\n";
    }
    for (var i = 0; i < skyPoints.length; ++i)
    {
        var c = opened.imageToCelestial(skyPoints[i][0], skyPoints[i][1]);
        var ra = Math.round(c.x * 1e6) / 1e6, dec = Math.round(c.y * 1e6) / 1e6;
        var p = opened.celestialToImage(ra, dec);
        csv += "celestial," + file + "," + num(ra) + "," + num(dec) + "," + (p ? num(p.x) + "," + num(p.y) : ",") +
               "\n";
    }
    opened.forceClose();
    File.remove(path);
}

var samples = [
    { file: "d5-astrometry.xisf", points: 1000, engine: "DDM", rbf: "DDMThinPlateSpline", order: 2 },
    { file: "d5-astrometry-local.xisf", points: 800, engine: "Recursive", rbf: "VariableOrder", order: 4 },
    { file: "d5-astrometry-multiquadric.xisf", points: 1000, engine: "DDM", rbf: "DDMMultiquadric", order: 2 },
];

function writeSample(sample, index)
{
    seed = 12345 + index;
    var window = makeWindow("openxisf_sample_d5_" + index, width, height);
    addSolution(window, sample);
    var path = outputDirectory + "/" + sample.file;
    if (!window.saveAs(path, false, false, false, false, "compression-codec zstd+sh"))
        throw new Error("cannot save " + path);
    window.forceClose();
    note("wrote " + path + " with the format hints 'compression-codec zstd+sh'");
    var opened = ImageWindow.open(path)[0];
    var ids = opened.mainView.properties;
    for (var i = 0; i < ids.length; ++i)
        if (ids[i].indexOf("AstrometricSolution:DistortionModel") >= 0)
            note("  " + describe(opened.mainView, ids[i]));
    evaluate(opened, sample.file);
    opened.forceClose();
}

// Linear solutions: the first layer only, about the centre of a 400 x 300 image.
var systems = ["Gnomonic", "Stereographic", "ZenithalEqualArea", "Orthographic", "PlateCarree", "Mercator",
               "HammerAitoff"];
var centres = [
    // ra0, dec0, degrees per pixel, rotation in degrees
    [83.82208, -5.39111, 0.01, 12],
    [30, 45, 0.1, -30],
    [200, -60, 0.16, 75],
    [0, 0, 0.2, 0],
    [350, 89.5, 0.04, 160],
    [120, -89.9, 0.04, -100],
    [10, 90, 0.02, 45],
    [250, 20, 0.3, 5],
];

function writeProjections()
{
    var text = "";
    for (var s = 0; s < systems.length; ++s)
        for (var c = 0; c < centres.length; ++c)
        {
            var k = centres[c];
            var name = systems[s] + "-" + c;
            var window = makeWindow("openxisf_d5_linear", 16, 16);
            var view = window.mainView;
            var t = k[3] / R;
            var L = new Matrix(2, 2);
            L[0][0] = -k[2] * Math.cos(t);
            L[0][1] = k[2] * Math.sin(t);
            L[1][0] = -k[2] * Math.sin(t);
            L[1][1] = -k[2] * Math.cos(t);
            view.beginProcess(UndoFlag.NoSwapFile);
            setProperty(view, "AstrometricSolution:Version", "1.0", PropertyType.String);
            setProperty(view, "AstrometricSolution:ProjectionSystem", systems[s], PropertyType.IsoString);
            setProperty(view, "AstrometricSolution:ReferenceCelestialCoordinates", new Vector([k[0], k[1]]),
                        PropertyType.F64Vector);
            setProperty(view, "AstrometricSolution:ReferenceImageCoordinates", new Vector([x0, y0]),
                        PropertyType.F64Vector);
            setProperty(view, "AstrometricSolution:LinearTransformationMatrix", L, PropertyType.F64Matrix);
            view.endProcess();
            window.regenerateAstrometricSolution();
            if (!window.hasAstrometricSolution)
                throw new Error("PixInsight recognizes no solution for " + name);
            text += "solution," + name + "," + systems[s] + "," + num(k[0]) + "," + num(k[1]) + "," + num(L[0][0]) +
                    "," + num(L[0][1]) + "," + num(L[1][0]) + "," + num(L[1][1]) + "," + num(x0) + "," + num(y0) +
                    "\n";
            var positions = [];
            for (var j = 0; j <= 2; ++j)
                for (var i = 0; i <= 2; ++i)
                {
                    var x = i * width / 2 + 0.25, y = j * height / 2 + 0.75;
                    var p = window.imageToCelestial(x, y);
                    text += "image," + name + "," + num(x) + "," + num(y) + "," +
                            (p ? num(p.x) + "," + num(p.y) : ",") + "\n";
                    if (p && (i + j) % 2 == 0)
                        positions.push([Math.round(p.x * 1e6) / 1e6, Math.round(p.y * 1e6) / 1e6]);
                }
            // A point more than 100 degrees from the reference point, beyond the hemisphere of the Gnomonic and
            // Orthographic projections, and away from the singular point opposite the reference point.
            positions.push([(k[0] + 100) % 360, -k[1] / 2]);
            for (var i = 0; i < positions.length; ++i)
            {
                var q = window.celestialToImage(positions[i][0], positions[i][1]);
                text += "celestial," + name + "," + num(positions[i][0]) + "," + num(positions[i][1]) + "," +
                        (q ? num(q.x) + "," + num(q.y) : ",") + "\n";
            }
            window.forceClose();
        }
    var header = "# " + creatorApplication() + ": linear solutions (layer 1) with default native " +
                 "coordinates, ReferenceImageCoordinates (200, 150).\n" +
                 "# solution,<name>,<projection>,<ra0>,<dec0>,<m00>,<m01>,<m10>,<m11>,<x0>,<y0>\n" +
                 "# image,<name>,<x>,<y>,<ra>,<dec>   (empty: no celestial coordinates)\n" +
                 "# celestial,<name>,<ra>,<dec>,<x>,<y>   (empty: no image coordinates)\n";
    File.writeTextFile(outputDirectory + "/d5-projections.csv", header + text);
    note("wrote d5-projections.csv");
}

try
{
    note(creatorApplication() + " build " + CoreApplication.versionBuild);
    for (var i = 0; i < samples.length; ++i)
        writeSample(samples[i], i);
    var header = "# " + creatorApplication() + ": the solutions of the D5 units, evaluated exactly.\n" +
                 "# image,<file>,<x>,<y>,<ra>,<dec>\n" +
                 "# celestial,<file>,<ra>,<dec>,<x>,<y>\n";
    File.writeTextFile(outputDirectory + "/d5-reference.csv", header + csv);
    note("wrote d5-reference.csv");
    writeProjections();
    note("done");
}
catch (failure)
{
    note("error: " + failure);
}

File.writeTextFile(outputDirectory + "/group_d5.log", log);
