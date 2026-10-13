"use strict";
(() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropSymbols = Object.getOwnPropertySymbols;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __propIsEnum = Object.prototype.propertyIsEnumerable;
  var __defNormalProp = (obj, key, value) => key in obj ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value }) : obj[key] = value;
  var __spreadValues = (a, b) => {
    for (var prop2 in b || (b = {}))
      if (__hasOwnProp.call(b, prop2))
        __defNormalProp(a, prop2, b[prop2]);
    if (__getOwnPropSymbols)
      for (var prop2 of __getOwnPropSymbols(b)) {
        if (__propIsEnum.call(b, prop2))
          __defNormalProp(a, prop2, b[prop2]);
      }
    return a;
  };

  // src/shared/messages.ts
  var DEFAULT_OPTIONS = {
    scale: 2,
    includeHidden: true,
    rasterizeUnsupported: true,
    variantPrefixes: ["Btn/", "Button/"]
  };
  var DEFAULT_LIVE_PORT = 7820;

  // src/shared/bundle.ts
  var BUNDLE_FORMAT = "figmasync";
  var BUNDLE_VERSION = 1;
  var BUNDLE_IMAGE_DIR = "images";

  // src/main/geometry.ts
  function invert(m) {
    const [[a, c, e], [b, d, f]] = m;
    const det = a * d - b * c;
    if (Math.abs(det) < 1e-12) {
      return [
        [1, 0, -e],
        [0, 1, -f]
      ];
    }
    const ia = d / det;
    const ic = -c / det;
    const ib = -b / det;
    const id = a / det;
    return [
      [ia, ic, -(ia * e + ic * f)],
      [ib, id, -(ib * e + id * f)]
    ];
  }
  function multiply(a, b) {
    return [
      [
        a[0][0] * b[0][0] + a[0][1] * b[1][0],
        a[0][0] * b[0][1] + a[0][1] * b[1][1],
        a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2]
      ],
      [
        a[1][0] * b[0][0] + a[1][1] * b[1][0],
        a[1][0] * b[0][1] + a[1][1] * b[1][1],
        a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2]
      ]
    ];
  }
  function apply(m, x, y) {
    return { x: m[0][0] * x + m[0][1] * y + m[0][2], y: m[1][0] * x + m[1][1] * y + m[1][2] };
  }
  function relativeTransform(parentAbs, childAbs) {
    return multiply(invert(parentAbs), childAbs);
  }
  function layoutBoxFromTransform(rel, width, height) {
    const [[a, c, e], [b, d, f]] = rel;
    const cx = a * (width / 2) + c * (height / 2) + e;
    const cy = b * (width / 2) + d * (height / 2) + f;
    const det = a * d - b * c;
    const rotation = normalizeAngle(Math.atan2(b, a) * 180 / Math.PI);
    return {
      x: round2(cx - width / 2),
      y: round2(cy - height / 2),
      width: round2(width),
      height: round2(height),
      rotation: round2(rotation),
      flipped: det < 0
    };
  }
  function rectRelativeTo(parentAbs, abs) {
    const inv = invert(parentAbs);
    const corners = [
      apply(inv, abs.x, abs.y),
      apply(inv, abs.x + abs.width, abs.y),
      apply(inv, abs.x, abs.y + abs.height),
      apply(inv, abs.x + abs.width, abs.y + abs.height)
    ];
    const xs = corners.map((p) => p.x);
    const ys = corners.map((p) => p.y);
    const minX = Math.min(...xs);
    const minY = Math.min(...ys);
    return {
      x: round2(minX),
      y: round2(minY),
      width: round2(Math.max(...xs) - minX),
      height: round2(Math.max(...ys) - minY)
    };
  }
  function normalizeAngle(deg) {
    let r = deg % 360;
    if (r <= -180) r += 360;
    if (r > 180) r -= 360;
    return r;
  }
  function round2(v) {
    const r = Math.round(v * 100) / 100;
    return Object.is(r, -0) ? 0 : r;
  }
  function isAxisAligned(rotation) {
    const r = Math.abs(normalizeAngle(rotation));
    return r < 0.01 || Math.abs(r - 90) < 0.01 || Math.abs(r - 180) < 0.01;
  }

  // src/main/hash.ts
  function hashBytes(data, seed = 0) {
    let h1 = 3735928559 ^ seed;
    let h2 = 1103547991 ^ seed;
    for (let i = 0; i < data.length; i++) {
      const ch = data[i];
      h1 = Math.imul(h1 ^ ch, 2654435761);
      h2 = Math.imul(h2 ^ ch, 1597334677);
    }
    h1 = Math.imul(h1 ^ h1 >>> 16, 2246822507);
    h1 ^= Math.imul(h2 ^ h2 >>> 13, 3266489909);
    h2 = Math.imul(h2 ^ h2 >>> 16, 2246822507);
    h2 ^= Math.imul(h1 ^ h1 >>> 13, 3266489909);
    return (h2 >>> 0).toString(16).padStart(8, "0") + (h1 >>> 0).toString(16).padStart(8, "0");
  }
  function bytesEqual(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
      if (a[i] !== b[i]) return false;
    }
    return true;
  }
  function pngSize(bytes) {
    const signature = [137, 80, 78, 71, 13, 10, 26, 10];
    if (bytes.length < 24 || signature.some((v, i) => bytes[i] !== v)) return null;
    const read32 = (o) => (bytes[o] << 24 | bytes[o + 1] << 16 | bytes[o + 2] << 8 | bytes[o + 3]) >>> 0;
    return { width: read32(16), height: read32(20) };
  }

  // src/main/paint.ts
  var NORMAL_BLENDS = /* @__PURE__ */ new Set(["NORMAL", "PASS_THROUGH"]);
  var GRADIENTS = /* @__PURE__ */ new Set(["GRADIENT_LINEAR", "GRADIENT_RADIAL", "GRADIENT_ANGULAR", "GRADIENT_DIAMOND"]);
  var KNOWN_PAINTS = /* @__PURE__ */ new Set(["SOLID", ...GRADIENTS, "IMAGE", "VIDEO", "PATTERN"]);
  function round4(v) {
    return Math.round(v * 1e4) / 1e4;
  }
  function color(r, g, b, a = 1) {
    return { r: round4(r), g: round4(g), b: round4(b), a: round4(a) };
  }
  function serializePaints(paints) {
    if (!paints || typeof paints === "symbol") return [];
    return paints.map(serializePaint);
  }
  function serializePaint(p) {
    var _a, _b;
    const type = KNOWN_PAINTS.has(p.type) ? p.type : "OTHER";
    const out = {
      type,
      visible: p.visible !== false,
      opacity: round4((_a = p.opacity) != null ? _a : 1),
      blendMode: (_b = p.blendMode) != null ? _b : "NORMAL"
    };
    if (p.type === "SOLID") {
      out.color = color(p.color.r, p.color.g, p.color.b, 1);
    } else if (GRADIENTS.has(p.type)) {
      const g = p;
      out.stops = g.gradientStops.map((s) => ({ position: round4(s.position), color: color(s.color.r, s.color.g, s.color.b, s.color.a) }));
    } else if (p.type === "IMAGE") {
      out.scaleMode = p.scaleMode;
    }
    return out;
  }
  function serializeEffects(effects) {
    if (!effects) return [];
    return effects.map((e) => {
      var _a;
      switch (e.type) {
        case "DROP_SHADOW":
        case "INNER_SHADOW":
          return {
            type: e.type,
            visible: e.visible !== false,
            radius: round2(e.radius),
            color: color(e.color.r, e.color.g, e.color.b, e.color.a),
            offset: { x: round2(e.offset.x), y: round2(e.offset.y) },
            spread: round2((_a = e.spread) != null ? _a : 0)
          };
        case "LAYER_BLUR":
        case "BACKGROUND_BLUR":
          return { type: e.type, visible: e.visible !== false, radius: round2(e.radius) };
        default:
          return { type: "OTHER", visible: e.visible !== false, radius: 0 };
      }
    });
  }
  function isVisiblePaint(p) {
    return p.visible && p.opacity > 5e-4;
  }
  function compositeSolids(paints) {
    let r = 0;
    let g = 0;
    let b = 0;
    let a = 0;
    let any = false;
    for (const p of paints) {
      if (p.type !== "SOLID" || !p.color || !isVisiblePaint(p)) continue;
      any = true;
      const sa = p.opacity * p.color.a;
      const outA = sa + a * (1 - sa);
      if (outA <= 0) continue;
      r = (p.color.r * sa + r * a * (1 - sa)) / outA;
      g = (p.color.g * sa + g * a * (1 - sa)) / outA;
      b = (p.color.b * sa + b * a * (1 - sa)) / outA;
      a = outA;
    }
    if (!any || a <= 0) return null;
    return { color: color(r, g, b, 1), opacity: round4(a) };
  }
  function classifyStyle(input) {
    const fills = input.fills.filter(isVisiblePaint);
    const strokes = input.strokes.filter(isVisiblePaint);
    const effects = input.effects.filter((e) => e.visible);
    for (const p of [...fills, ...strokes]) {
      if (p.type === "IMAGE" || p.type === "VIDEO" || p.type === "PATTERN") {
        return { simple: false, reason: "imageFill", detail: `${p.type.toLowerCase()} paint` };
      }
      if (GRADIENTS.has(p.type)) {
        return { simple: false, reason: "gradient", detail: p.type.toLowerCase().replace("_", " ") };
      }
      if (p.type === "OTHER") {
        return { simple: false, reason: "unsupported", detail: "unknown paint type" };
      }
      if (!NORMAL_BLENDS.has(p.blendMode)) {
        return { simple: false, reason: "unsupported", detail: `paint blend mode ${p.blendMode}` };
      }
    }
    if (strokes.length > 1) return { simple: false, reason: "unsupported", detail: "multiple strokes" };
    if (strokes.length === 1 && !input.uniformStroke) return { simple: false, reason: "unsupported", detail: "per-side stroke weights" };
    if (strokes.length === 1 && input.dashed) return { simple: false, reason: "unsupported", detail: "dashed stroke" };
    if (effects.length > 0) {
      return { simple: false, reason: "effects", detail: effects.map((e) => e.type.toLowerCase().replace("_", " ")).join(", ") };
    }
    if (!NORMAL_BLENDS.has(input.blendMode)) return { simple: false, reason: "unsupported", detail: `layer blend mode ${input.blendMode}` };
    return { simple: true };
  }
  function firstSolidColor(paints) {
    for (let i = paints.length - 1; i >= 0; i--) {
      const p = paints[i];
      if (p.type === "SOLID" && p.color && isVisiblePaint(p)) {
        return color(p.color.r, p.color.g, p.color.b, p.color.a * p.opacity);
      }
    }
    for (const p of paints) {
      if (p.stops && p.stops.length > 0 && isVisiblePaint(p)) {
        const c = p.stops[0].color;
        return color(c.r, c.g, c.b, c.a * p.opacity);
      }
    }
    return null;
  }

  // src/main/tags.ts
  var TAG_GROUP = /\[([^\[\]]*)\]/g;
  var SCALE_TOKEN = /^@?(\d+(?:\.\d+)?)x$/i;
  var KEY_WITH_SCALE = /^([A-Za-z_][\w-]*)@(\d+(?:\.\d+)?)x$/i;
  var VALUE_WITH_SCALE = /^(.*)@(\d+(?:\.\d+)?)x$/i;
  var KEY_ALIASES = {
    image: "img",
    raster: "img",
    ignore: "skip",
    btn: "button",
    variable: "var",
    "9slice": "slice"
  };
  function normalizeTagKey(key) {
    var _a;
    const lower = key.toLowerCase();
    return (_a = KEY_ALIASES[lower]) != null ? _a : lower;
  }
  function parseLayerName(raw) {
    const tags = {};
    const withoutTags = raw.replace(TAG_GROUP, (_match, body) => {
      parseTagBody(body, tags);
      return " ";
    });
    const cleanName = withoutTags.replace(/\s+/g, " ").trim();
    return { cleanName, tags };
  }
  function parseTagBody(body, out) {
    var _a;
    const tokens = body.split(/[\s,]+/).filter((t) => t.length > 0);
    for (const token of tokens) {
      const scaleOnly = SCALE_TOKEN.exec(token);
      if (scaleOnly) {
        out.scale = scaleOnly[1];
        continue;
      }
      const keyWithScale = KEY_WITH_SCALE.exec(token);
      if (keyWithScale) {
        out[normalizeTagKey(keyWithScale[1])] = (_a = out[normalizeTagKey(keyWithScale[1])]) != null ? _a : "";
        out.scale = keyWithScale[2];
        continue;
      }
      const sep = token.search(/[=:]/);
      if (sep > 0) {
        const key2 = normalizeTagKey(token.slice(0, sep));
        let value = token.slice(sep + 1);
        const valueWithScale = VALUE_WITH_SCALE.exec(value);
        if (valueWithScale) {
          value = valueWithScale[1];
          out.scale = valueWithScale[2];
        }
        out[key2] = value;
        continue;
      }
      const key = normalizeTagKey(token);
      if (!(key in out)) {
        out[key] = "";
      }
    }
  }
  function tagScale(tags) {
    if (tags.scale === void 0) return void 0;
    const value = Number(tags.scale);
    return Number.isFinite(value) && value > 0 && value <= 8 ? value : void 0;
  }

  // src/main/walker.ts
  var GENERATOR_NAME = "widgetify-figma";
  var GENERATOR_VERSION = "0.1.0";
  var VECTOR_TYPES = /* @__PURE__ */ new Set(["VECTOR", "STAR", "POLYGON", "BOOLEAN_OPERATION", "TEXT_PATH"]);
  var KNOWN_TYPES = /* @__PURE__ */ new Set([
    "FRAME",
    "GROUP",
    "COMPONENT",
    "COMPONENT_SET",
    "INSTANCE",
    "SECTION",
    "RECTANGLE",
    "ELLIPSE",
    "LINE",
    "TEXT",
    "VECTOR",
    "STAR",
    "POLYGON",
    "BOOLEAN_OPERATION"
  ]);
  var NEVER_EXPORT = /* @__PURE__ */ new Set(["SLICE", "CONNECTOR", "STICKY", "STAMP", "WIDGET", "EMBED", "LINK_UNFURL"]);
  var STATE_WORDS = /* @__PURE__ */ new Set(["default", "normal", "rest", "idle", "hover", "hovered", "pressed", "down", "active", "disabled", "selected", "focused", "focus"]);
  var TEXT_SEGMENT_FIELDS = ["fontName", "fontSize", "fontWeight", "fills", "letterSpacing", "lineHeight", "textCase", "textDecoration", "textStyleId"];
  function prop(node, key) {
    if (!(key in node)) return void 0;
    const value = node[key];
    return typeof value === "symbol" ? void 0 : value;
  }
  function hasChildren(node) {
    return "children" in node && Array.isArray(node.children);
  }
  function sanitizeFileName(name) {
    const cleaned = name.normalize("NFKD").replace(/[^\w-]+/g, "_").replace(/_+/g, "_").replace(/^_|_$/g, "");
    return cleaned.length > 0 ? cleaned.slice(0, 64) : "image";
  }
  var AssetCollector = class {
    constructor() {
      this.files = [];
      this.assets = [];
      this.byHash = /* @__PURE__ */ new Map();
      this.usedNames = /* @__PURE__ */ new Set();
    }
    add(bytes, baseName, scale) {
      var _a, _b;
      const hash = hashBytes(bytes);
      const candidates = (_a = this.byHash.get(hash)) != null ? _a : [];
      for (const c of candidates) {
        if (c.def.scale === scale && bytesEqual(c.bytes, bytes)) return c.def;
      }
      let name = sanitizeFileName(baseName);
      let suffix = 2;
      while (this.usedNames.has(name.toLowerCase())) {
        name = `${sanitizeFileName(baseName)}_${suffix++}`;
      }
      this.usedNames.add(name.toLowerCase());
      const size = (_b = pngSize(bytes)) != null ? _b : { width: 0, height: 0 };
      const def = {
        id: `img${this.assets.length + 1}`,
        path: `${BUNDLE_IMAGE_DIR}/${name}.png`,
        width: size.width,
        height: size.height,
        scale,
        hash,
        bytes: bytes.length
      };
      this.assets.push(def);
      this.files.push({ path: def.path, data: bytes });
      candidates.push({ def, bytes });
      this.byHash.set(hash, candidates);
      return def;
    }
  };
  var FrameExporter = class {
    constructor(options, host2) {
      this.options = options;
      this.host = host2;
      this.assets = new AssetCollector();
      this.warnings = [];
      this.components = {};
      this.componentsInProgress = /* @__PURE__ */ new Set();
      this.styleNames = /* @__PURE__ */ new Map();
      this.done = 0;
      this.total = 1;
      this.nodeCount = 0;
    }
    async run(root, meta) {
      var _a;
      this.total = Math.max(1, this.countNodes(root));
      const rootAbs = root.absoluteTransform;
      const rootCtx = { parentAbs: rootAbs, parentAutoLayout: false, inGroup: false, path: "" };
      const rootNode = await this.serialize(root, rootCtx, true);
      if (!rootNode) {
        throw new Error("The selected layer is hidden or tagged [skip], so there is nothing to export.");
      }
      const bundle = {
        format: BUNDLE_FORMAT,
        version: BUNDLE_VERSION,
        generator: { name: GENERATOR_NAME, version: GENERATOR_VERSION },
        exportedAt: (_a = meta.exportedAt) != null ? _a : (/* @__PURE__ */ new Date()).toISOString(),
        source: { fileName: meta.fileName, pageName: meta.pageName, frameId: root.id, frameName: rootNode.cleanName },
        options: this.options,
        root: rootNode,
        components: this.components,
        assets: this.assets.assets,
        warnings: this.warnings
      };
      return {
        bundle,
        json: JSON.stringify(bundle, null, 2),
        files: this.assets.files,
        stats: { nodes: this.nodeCount, images: this.assets.assets.length }
      };
    }
    countNodes(node) {
      const { tags } = parseLayerName(node.name);
      if ("skip" in tags) return 0;
      if (!node.visible && !this.options.includeHidden) return 0;
      let n = 1;
      if (hasChildren(node) && !("img" in tags)) {
        for (const c of node.children) n += this.countNodes(c);
      }
      return n;
    }
    warn(ctxPath, node, message) {
      this.warnings.push({ nodeId: node == null ? void 0 : node.id, nodePath: ctxPath || (node == null ? void 0 : node.name), message });
    }
    async resolveStyle(id) {
      if (typeof id !== "string" || id.length === 0) return void 0;
      if (!this.styleNames.has(id)) {
        try {
          this.styleNames.set(id, await this.host.styleName(id));
        } catch (e) {
          this.styleNames.set(id, void 0);
        }
      }
      return this.styleNames.get(id);
    }
    async serialize(node, ctx, isRoot = false) {
      var _a, _b, _c;
      if (NEVER_EXPORT.has(node.type)) return null;
      const { cleanName, tags } = parseLayerName(node.name);
      if ("skip" in tags) return null;
      if (!node.visible && !this.options.includeHidden) return null;
      const path = ctx.path ? `${ctx.path}/${cleanName || node.name}` : cleanName || node.name;
      this.done++;
      this.nodeCount++;
      this.host.progress(Math.min(this.done, this.total), this.total, path);
      const width = (_a = prop(node, "width")) != null ? _a : 0;
      const height = (_b = prop(node, "height")) != null ? _b : 0;
      const rel = relativeTransform(ctx.parentAbs, node.absoluteTransform);
      const box = layoutBoxFromTransform(rel, width, height);
      const type = KNOWN_TYPES.has(node.type) ? node.type : "OTHER";
      const out = {
        id: node.id,
        name: node.name,
        cleanName: cleanName || `${node.type.toLowerCase()}_${node.id.replace(/[^\w]+/g, "_")}`,
        tags,
        type,
        visible: node.visible,
        opacity: round2((_c = prop(node, "opacity")) != null ? _c : 1),
        rotation: box.rotation,
        x: box.x,
        y: box.y,
        width: box.width,
        height: box.height
      };
      if (!isRoot) {
        this.applyPlacement(node, ctx, out);
      }
      const limits = this.sizeLimits(node);
      if (limits) out.sizeLimits = limits;
      const clips = prop(node, "clipsContent");
      if (clips !== void 0 && hasChildren(node)) out.clipsContent = clips;
      const refs = prop(node, "componentPropertyReferences");
      if (refs) {
        const cleaned = {};
        for (const [k, v] of Object.entries(refs)) if (typeof v === "string") cleaned[k] = stripPropId(v);
        if (Object.keys(cleaned).length > 0) out.propRefs = cleaned;
      }
      if (prop(node, "isMask")) out.isMask = true;
      if (node.type === "INSTANCE" || node.type === "COMPONENT" || node.type === "COMPONENT_SET") {
        out.component = await this.componentRef(node, path);
      }
      if (node.type === "TEXT") {
        out.text = await this.textInfo(node, path);
      } else if ("fills" in node || "strokes" in node) {
        out.style = await this.nodeStyle(node);
      }
      if (box.flipped) {
        this.warn(path, node, "Layer is mirrored; UMG render transforms cannot flip, so it was rasterized.");
      }
      const rasterReason = this.wholeNodeRasterReason(node, out, tags, box.flipped, isRoot);
      if (rasterReason) {
        await this.rasterizeWhole(node, out, ctx, tags, rasterReason, path);
        return out;
      }
      if (out.style && !out.style.simple && hasChildren(node)) {
        if (this.options.rasterizeUnsupported) {
          await this.rasterizeBackground(node, out, tags, path);
        } else {
          this.warn(path, node, `Background uses ${this.styleProblem(out.style)}; it will be approximated with a flat color.`);
        }
      } else if (out.style && !out.style.simple && !this.options.rasterizeUnsupported) {
        this.warn(path, node, `Uses ${this.styleProblem(out.style)}; it will be approximated with a flat color.`);
      }
      if (hasChildren(node)) {
        const layoutMode = prop(node, "layoutMode");
        const auto = layoutMode === "HORIZONTAL" || layoutMode === "VERTICAL";
        if (auto) {
          out.layout = this.autoLayout(node);
        } else if (layoutMode === "GRID") {
          this.warn(path, node, "Grid auto-layout is not supported yet; children keep their absolute positions.");
        }
        const childCtx = {
          parentAbs: node.absoluteTransform,
          parentAutoLayout: auto,
          inGroup: node.type === "GROUP" || node.type === "BOOLEAN_OPERATION",
          path
        };
        const children = [];
        for (const child of node.children) {
          if (prop(child, "isMask") && isRoot) {
            this.warn(path, child, "Masks directly inside the exported frame are not supported; the mask shape was skipped.");
            continue;
          }
          const c = await this.serialize(child, childCtx);
          if (c) children.push(c);
        }
        if (node.type === "GROUP" && !isRoot && out.constraints && !ctx.inGroup) {
          out.constraints = groupConstraints(node.children);
        }
        out.children = children;
      }
      return out;
    }
    applyPlacement(node, ctx, out) {
      var _a;
      const positioning = (_a = prop(node, "layoutPositioning")) != null ? _a : "AUTO";
      if (ctx.parentAutoLayout) {
        out.child = this.childLayout(node, positioning);
      }
      if (!ctx.parentAutoLayout || positioning === "ABSOLUTE") {
        if (ctx.inGroup) {
          out.constraints = { horizontal: "SCALE", vertical: "SCALE" };
        } else {
          const c = prop(node, "constraints");
          out.constraints = c ? { horizontal: c.horizontal, vertical: c.vertical } : { horizontal: "MIN", vertical: "MIN" };
        }
      }
    }
    childLayout(node, positioning) {
      var _a, _b, _c, _d;
      const grow = (_a = prop(node, "layoutGrow")) != null ? _a : 0;
      const align = (_b = prop(node, "layoutAlign")) != null ? _b : "INHERIT";
      const sizingH = (_c = prop(node, "layoutSizingHorizontal")) != null ? _c : "FIXED";
      const sizingV = (_d = prop(node, "layoutSizingVertical")) != null ? _d : "FIXED";
      return { positioning, sizingH, sizingV, grow: round2(grow), alignSelf: align === "STRETCH" ? "STRETCH" : "INHERIT" };
    }
    sizeLimits(node) {
      const limits = {};
      for (const key of ["minWidth", "maxWidth", "minHeight", "maxHeight"]) {
        const v = prop(node, key);
        if (typeof v === "number") limits[key] = round2(v);
      }
      return Object.keys(limits).length > 0 ? limits : void 0;
    }
    autoLayout(node) {
      var _a, _b, _c, _d, _e, _f, _g, _h, _i, _j;
      return {
        mode: (_a = prop(node, "layoutMode")) != null ? _a : "HORIZONTAL",
        wrap: prop(node, "layoutWrap") === "WRAP",
        padding: {
          left: round2((_b = prop(node, "paddingLeft")) != null ? _b : 0),
          right: round2((_c = prop(node, "paddingRight")) != null ? _c : 0),
          top: round2((_d = prop(node, "paddingTop")) != null ? _d : 0),
          bottom: round2((_e = prop(node, "paddingBottom")) != null ? _e : 0)
        },
        itemSpacing: round2((_f = prop(node, "itemSpacing")) != null ? _f : 0),
        counterAxisSpacing: round2((_g = prop(node, "counterAxisSpacing")) != null ? _g : 0),
        primaryAlign: (_h = prop(node, "primaryAxisAlignItems")) != null ? _h : "MIN",
        counterAlign: (_i = prop(node, "counterAxisAlignItems")) != null ? _i : "MIN",
        reverseZIndex: (_j = prop(node, "itemReverseZIndex")) != null ? _j : false
      };
    }
    async nodeStyle(node) {
      var _a, _b, _c;
      const fills = serializePaints(prop(node, "fills"));
      const strokes = serializePaints(prop(node, "strokes"));
      const effects = serializeEffects(prop(node, "effects"));
      const strokeWeight = prop(node, "strokeWeight");
      const sides = {
        top: prop(node, "strokeTopWeight"),
        right: prop(node, "strokeRightWeight"),
        bottom: prop(node, "strokeBottomWeight"),
        left: prop(node, "strokeLeftWeight")
      };
      const sideValues = [sides.top, sides.right, sides.bottom, sides.left].filter((v) => typeof v === "number");
      const uniform = sideValues.length < 4 || sideValues.every((v) => Math.abs(v - sideValues[0]) < 1e-3);
      const dash = prop(node, "dashPattern");
      const dashed = !!dash && dash.length > 0;
      const blendMode = (_a = prop(node, "blendMode")) != null ? _a : "PASS_THROUGH";
      const style = {
        fills,
        strokes,
        strokeWeight: round2(strokeWeight != null ? strokeWeight : sideValues.length ? Math.max(...sideValues) : 0),
        strokeAlign: (_b = prop(node, "strokeAlign")) != null ? _b : "INSIDE",
        dashed,
        cornerRadius: cornerRadii(node),
        cornerSmoothing: round2((_c = prop(node, "cornerSmoothing")) != null ? _c : 0),
        effects,
        blendMode,
        simple: false
      };
      if (!uniform && sideValues.length === 4) {
        style.strokeWeights = { top: round2(sides.top), right: round2(sides.right), bottom: round2(sides.bottom), left: round2(sides.left) };
      }
      style.fillStyle = await this.resolveStyle(prop(node, "fillStyleId"));
      style.strokeStyle = await this.resolveStyle(prop(node, "strokeStyleId"));
      style.effectStyle = await this.resolveStyle(prop(node, "effectStyleId"));
      for (const key of ["fillStyle", "strokeStyle", "effectStyle"]) {
        if (style[key] === void 0) delete style[key];
      }
      const verdict = classifyStyle({ fills, strokes, effects, uniformStroke: uniform, dashed, blendMode });
      style.simple = verdict.simple;
      if (verdict.simple) {
        const solid = compositeSolids(fills);
        style.fills = solid ? [{ type: "SOLID", visible: true, opacity: solid.opacity, blendMode: "NORMAL", color: solid.color }] : [];
        style.strokes = strokes.filter(isVisiblePaint);
      }
      return style;
    }
    styleProblem(style) {
      var _a;
      const verdict = classifyStyle({
        fills: style.fills,
        strokes: style.strokes,
        effects: style.effects,
        uniformStroke: !style.strokeWeights,
        dashed: style.dashed,
        blendMode: style.blendMode
      });
      return (_a = verdict.detail) != null ? _a : "an unsupported effect";
    }
    async textInfo(node, path) {
      var _a, _b, _c, _d, _e, _f, _g, _h, _i, _j, _k, _l, _m, _n, _o, _p, _q, _r;
      let segments = [];
      try {
        segments = node.getStyledTextSegments([...TEXT_SEGMENT_FIELDS]);
      } catch (e) {
        segments = [];
      }
      let dominant;
      for (const s of segments) {
        if (!dominant || s.end - s.start > dominant.end - dominant.start) dominant = s;
      }
      const fontName = (_a = dominant == null ? void 0 : dominant.fontName) != null ? _a : typeof node.fontName === "symbol" ? { family: "Inter", style: "Regular" } : node.fontName;
      const fills = serializePaints((_b = dominant == null ? void 0 : dominant.fills) != null ? _b : prop(node, "fills"));
      const textColor = (_c = firstSolidColor(fills)) != null ? _c : color(0, 0, 0, 1);
      if (fills.some((p) => p.visible && p.type !== "SOLID")) {
        this.warn(path, node, "Text uses a gradient or image fill; using its first color. Tag it [img] to keep the exact look.");
      }
      const letterSpacing = (_e = (_d = dominant == null ? void 0 : dominant.letterSpacing) != null ? _d : prop(node, "letterSpacing")) != null ? _e : { value: 0, unit: "PIXELS" };
      const lineHeight = (_g = (_f = dominant == null ? void 0 : dominant.lineHeight) != null ? _f : prop(node, "lineHeight")) != null ? _g : { unit: "AUTO" };
      const info = {
        characters: node.characters,
        fontFamily: fontName.family,
        fontStyle: fontName.style,
        fontWeight: (_i = (_h = dominant == null ? void 0 : dominant.fontWeight) != null ? _h : prop(node, "fontWeight")) != null ? _i : 400,
        italic: /italic|oblique/i.test(fontName.style),
        fontSize: round2((_k = (_j = dominant == null ? void 0 : dominant.fontSize) != null ? _j : prop(node, "fontSize")) != null ? _k : 16),
        color: textColor,
        alignH: node.textAlignHorizontal,
        alignV: node.textAlignVertical,
        autoResize: node.textAutoResize,
        truncate: prop(node, "textTruncation") === "ENDING" || node.textAutoResize === "TRUNCATE",
        maxLines: (_l = prop(node, "maxLines")) != null ? _l : 0,
        letterSpacing: { value: round2(letterSpacing.value), unit: letterSpacing.unit },
        lineHeight: lineHeight.unit === "AUTO" ? { value: 0, unit: "AUTO" } : { value: round2(lineHeight.value), unit: lineHeight.unit },
        textCase: (_n = (_m = dominant == null ? void 0 : dominant.textCase) != null ? _m : prop(node, "textCase")) != null ? _n : "ORIGINAL",
        decoration: (_p = (_o = dominant == null ? void 0 : dominant.textDecoration) != null ? _o : prop(node, "textDecoration")) != null ? _p : "NONE",
        mixed: segments.length > 1
      };
      const textStyle = await this.resolveStyle((_q = dominant == null ? void 0 : dominant.textStyleId) != null ? _q : prop(node, "textStyleId"));
      if (textStyle) info.textStyle = textStyle;
      const strokes = serializePaints(prop(node, "strokes")).filter(isVisiblePaint);
      const strokeWeight = (_r = prop(node, "strokeWeight")) != null ? _r : 0;
      const strokeColor = firstSolidColor(strokes);
      if (strokeColor && strokeWeight > 0) info.stroke = { color: strokeColor, weight: round2(strokeWeight) };
      const effects = serializeEffects(prop(node, "effects")).filter((e) => e.visible);
      const shadow = effects.find((e) => e.type === "DROP_SHADOW");
      if (shadow && shadow.color && shadow.offset) info.shadow = { color: shadow.color, offset: shadow.offset, radius: shadow.radius };
      if (effects.some((e) => e.type !== "DROP_SHADOW")) {
        this.warn(path, node, "Text blur / inner shadow effects are not supported by UMG text and were dropped.");
      }
      if (info.mixed) {
        info.segments = segments.map(
          (s) => {
            var _a2;
            return {
              start: s.start,
              end: s.end,
              characters: s.characters,
              fontFamily: s.fontName.family,
              fontStyle: s.fontName.style,
              fontWeight: s.fontWeight,
              fontSize: round2(s.fontSize),
              color: (_a2 = firstSolidColor(serializePaints(s.fills))) != null ? _a2 : textColor
            };
          }
        );
        this.warn(path, node, `Text has ${segments.length} differently styled runs; Unreal gets the dominant style (${fontName.family} ${fontName.style}).`);
      }
      return info;
    }
    async componentRef(node, path) {
      if (node.type === "COMPONENT_SET") {
        return { kind: "COMPONENT_SET", componentSetId: node.id, componentSetName: node.name };
      }
      if (node.type === "COMPONENT") {
        const ref2 = { kind: "COMPONENT", componentId: node.id, componentName: node.name };
        const parent = node.parent;
        if (parent && parent.type === "COMPONENT_SET") {
          ref2.componentSetId = parent.id;
          ref2.componentSetName = parent.name;
          if (node.variantProperties) ref2.variant = __spreadValues({}, node.variantProperties);
        }
        return ref2;
      }
      const ref = { kind: "INSTANCE" };
      try {
        const props = node.componentProperties;
        const cleaned = {};
        for (const [key, value] of Object.entries(props != null ? props : {})) {
          cleaned[stripPropId(key)] = { type: value.type, value: value.value };
        }
        if (Object.keys(cleaned).length > 0) ref.properties = cleaned;
      } catch (e) {
      }
      let main = null;
      try {
        main = await this.host.mainComponent(node);
      } catch (e) {
        main = null;
      }
      if (!main) {
        this.warn(path, node, "Instance has no main component (deleted or inaccessible); exported as a plain frame.");
        return ref;
      }
      ref.componentId = main.id;
      ref.componentName = main.name;
      ref.remote = main.remote;
      let set = null;
      try {
        set = main.parent && main.parent.type === "COMPONENT_SET" ? main.parent : null;
      } catch (e) {
        set = null;
      }
      if (set) {
        ref.componentSetId = set.id;
        ref.componentSetName = set.name;
        try {
          if (main.variantProperties) ref.variant = __spreadValues({}, main.variantProperties);
        } catch (e) {
        }
        if (this.matchesVariantPrefix(set.name)) await this.collectComponentSet(set, path);
      } else if (this.matchesVariantPrefix(main.name)) {
        const virtual = virtualSetKey(main.name);
        if (virtual) {
          ref.componentSetId = `virtual:${virtual.key}`;
          ref.componentSetName = virtual.key;
          ref.variant = { State: virtual.state };
          await this.collectVirtualSet(main, virtual.key, path);
        }
      }
      return ref;
    }
    matchesVariantPrefix(name) {
      return this.options.variantPrefixes.some((p) => p.length > 0 && name.toLowerCase().startsWith(p.toLowerCase()));
    }
    async collectComponentSet(set, path) {
      var _a;
      if (this.components[set.id] || this.componentsInProgress.has(set.id)) return;
      this.componentsInProgress.add(set.id);
      try {
        await this.host.ensurePageLoaded(set);
        let propertyNames = [];
        try {
          propertyNames = Object.entries(set.componentPropertyDefinitions).filter(([, def2]) => def2.type === "VARIANT").map(([name]) => name);
        } catch (e) {
          propertyNames = [];
        }
        const def = { id: set.id, name: set.name, remote: set.remote, propertyNames, variants: [] };
        for (const child of set.children) {
          if (child.type !== "COMPONENT") continue;
          const variantCtx = { parentAbs: child.absoluteTransform, parentAutoLayout: false, inGroup: false, path: `${set.name}/${child.name}` };
          const node = await this.serialize(child, variantCtx, true);
          if (node) def.variants.push({ id: child.id, name: child.name, properties: __spreadValues({}, (_a = child.variantProperties) != null ? _a : {}), node });
        }
        this.components[set.id] = def;
      } catch (e) {
        this.warn(path, void 0, `Could not read variants of "${set.name}" (${String(e)}); button states will be derived in Unreal.`);
      } finally {
        this.componentsInProgress.delete(set.id);
      }
    }
    /** "Btn/Primary/Hover" style naming without a component set: siblings sharing the prefix become variants. */
    async collectVirtualSet(main, key, path) {
      const id = `virtual:${key}`;
      if (this.components[id] || this.componentsInProgress.has(id)) return;
      this.componentsInProgress.add(id);
      try {
        await this.host.ensurePageLoaded(main);
        const siblings = main.parent && "children" in main.parent ? main.parent.children : [main];
        const def = { id, name: key, remote: main.remote, propertyNames: ["State"], variants: [] };
        for (const sibling of siblings) {
          if (sibling.type !== "COMPONENT") continue;
          const v = virtualSetKey(sibling.name);
          if (!v || v.key !== key) continue;
          const ctx = { parentAbs: sibling.absoluteTransform, parentAutoLayout: false, inGroup: false, path: sibling.name };
          const node = await this.serialize(sibling, ctx, true);
          if (node) def.variants.push({ id: sibling.id, name: sibling.name, properties: { State: v.state }, node });
        }
        this.components[id] = def;
      } catch (e) {
        this.warn(path, void 0, `Could not collect states for "${key}" (${String(e)}).`);
      } finally {
        this.componentsInProgress.delete(id);
      }
    }
    wholeNodeRasterReason(node, out, tags, flipped, isRoot) {
      var _a, _b, _c, _d;
      if ("img" in tags) return "tag";
      if (isRoot) return void 0;
      if (flipped) return "flip";
      if (VECTOR_TYPES.has(node.type)) return "vector";
      if (out.type === "OTHER") return hasChildren(node) ? void 0 : "unsupported";
      if (node.type === "ELLIPSE") {
        const arc = prop(node, "arcData");
        const fullCircle = !arc || Math.abs(arc.endingAngle - arc.startingAngle - Math.PI * 2) < 1e-3 && arc.innerRadius === 0;
        const circle = Math.abs(out.width - out.height) < 0.5;
        if (!fullCircle || !circle) return "vector";
      }
      if (node.type === "LINE") {
        const strokes = (_b = (_a = out.style) == null ? void 0 : _a.strokes.filter(isVisiblePaint)) != null ? _b : [];
        if (!isAxisAligned(out.rotation) || strokes.length !== 1 || strokes[0].type !== "SOLID" || ((_c = out.style) == null ? void 0 : _c.dashed)) return "vector";
      }
      if (hasChildren(node) && node.children.some((c) => prop(c, "isMask"))) return "mask";
      if (!hasChildren(node) && out.style && !out.style.simple && this.options.rasterizeUnsupported) {
        const verdict = classifyStyle({
          fills: out.style.fills,
          strokes: out.style.strokes,
          effects: out.style.effects,
          uniformStroke: !out.style.strokeWeights,
          dashed: out.style.dashed,
          blendMode: out.style.blendMode
        });
        return (_d = verdict.reason) != null ? _d : "unsupported";
      }
      return void 0;
    }
    async rasterizeWhole(node, out, ctx, tags, reason, path) {
      var _a, _b;
      const scale = (_a = tagScale(tags)) != null ? _a : this.options.scale;
      try {
        const { bytes, absBounds } = await this.host.exportPng(node, scale);
        const asset = this.assets.add(bytes, out.cleanName, scale);
        const bbAbs = (_b = prop(node, "absoluteBoundingBox")) != null ? _b : null;
        let bounds = { x: 0, y: 0, width: out.width, height: out.height };
        if (out.rotation !== 0 || reason === "flip") {
          if (bbAbs) {
            const bb = rectRelativeTo(ctx.parentAbs, bbAbs);
            out.x = bb.x;
            out.y = bb.y;
            out.width = bb.width;
            out.height = bb.height;
          }
          out.rotation = 0;
        }
        if (absBounds) {
          const rb = rectRelativeTo(ctx.parentAbs, absBounds);
          bounds = { x: round2(rb.x - out.x), y: round2(rb.y - out.y), width: rb.width, height: rb.height };
        }
        out.image = { asset: asset.id, bounds, reason };
        delete out.children;
        delete out.layout;
      } catch (e) {
        this.warn(path, node, `PNG export failed (${String(e)}); the layer was exported without its image.`);
      }
    }
    async rasterizeBackground(node, out, tags, path) {
      var _a;
      const scale = (_a = tagScale(tags)) != null ? _a : this.options.scale;
      try {
        const { bytes, bounds } = await this.host.exportBackgroundPng(node, scale);
        const asset = this.assets.add(bytes, `${out.cleanName}_Bg`, scale);
        out.background = { asset: asset.id, bounds, reason: this.backgroundReason(out.style) };
      } catch (e) {
        this.warn(path, node, `Background export failed (${String(e)}); it will be approximated with a flat color.`);
      }
    }
    backgroundReason(style) {
      var _a;
      return (_a = classifyStyle({
        fills: style.fills,
        strokes: style.strokes,
        effects: style.effects,
        uniformStroke: !style.strokeWeights,
        dashed: style.dashed,
        blendMode: style.blendMode
      }).reason) != null ? _a : "unsupported";
    }
  };
  function cornerRadii(node) {
    var _a, _b, _c, _d;
    const tl = prop(node, "topLeftRadius");
    if (typeof tl === "number") {
      return [
        round2(tl),
        round2((_a = prop(node, "topRightRadius")) != null ? _a : 0),
        round2((_b = prop(node, "bottomRightRadius")) != null ? _b : 0),
        round2((_c = prop(node, "bottomLeftRadius")) != null ? _c : 0)
      ];
    }
    const r = round2((_d = prop(node, "cornerRadius")) != null ? _d : 0);
    return [r, r, r, r];
  }
  function stripPropId(name) {
    const hash = name.lastIndexOf("#");
    return hash > 0 ? name.slice(0, hash) : name;
  }
  function virtualSetKey(name) {
    const parts = name.split("/").map((p) => p.trim());
    if (parts.length < 3) return null;
    const state = parts[parts.length - 1];
    if (!STATE_WORDS.has(state.toLowerCase())) return null;
    return { key: parts.slice(0, -1).join("/"), state };
  }
  function groupConstraints(children) {
    let h;
    let v;
    let uniformH = true;
    let uniformV = true;
    for (const child of children) {
      const c = prop(child, "constraints");
      if (!c) continue;
      if (h === void 0) h = c.horizontal;
      else if (h !== c.horizontal) uniformH = false;
      if (v === void 0) v = c.vertical;
      else if (v !== c.vertical) uniformV = false;
    }
    return { horizontal: uniformH && h ? h : "MIN", vertical: uniformV && v ? v : "MIN" };
  }

  // src/main/code.ts
  var PREFS_KEY = "widgetify.prefs.v1";
  var EXPORTABLE_ROOTS = /* @__PURE__ */ new Set(["FRAME", "COMPONENT", "COMPONENT_SET", "INSTANCE", "SECTION", "GROUP"]);
  figma.showUI(__html__, { width: 380, height: 600, themeColors: true, title: "Widgetify \u2014 Figma \u2192 UMG" });
  function post(msg) {
    figma.ui.postMessage(msg);
  }
  function countLayers(node, limit = 5e3) {
    let n = 1;
    if ("children" in node) {
      for (const c of node.children) {
        n += countLayers(c, limit - n);
        if (n >= limit) return n;
      }
    }
    return n;
  }
  function describeSelection() {
    const sel = figma.currentPage.selection;
    if (sel.length === 0) return { ok: false, reason: "Select a frame to export." };
    if (sel.length > 1) return { ok: false, reason: "Select a single frame (one screen per bundle)." };
    const node = sel[0];
    if (!EXPORTABLE_ROOTS.has(node.type)) {
      return { ok: false, reason: `A ${node.type.toLowerCase().replace(/_/g, " ")} can't be a screen. Select its parent frame.` };
    }
    return {
      ok: true,
      id: node.id,
      name: node.name,
      type: node.type,
      width: Math.round(node.width),
      height: Math.round(node.height),
      layerCount: countLayers(node)
    };
  }
  async function loadPrefs() {
    var _a, _b;
    try {
      const stored = await figma.clientStorage.getAsync(PREFS_KEY);
      return {
        options: __spreadValues(__spreadValues({}, DEFAULT_OPTIONS), (_a = stored == null ? void 0 : stored.options) != null ? _a : {}),
        livePort: (_b = stored == null ? void 0 : stored.livePort) != null ? _b : DEFAULT_LIVE_PORT
      };
    } catch (e) {
      return { options: __spreadValues({}, DEFAULT_OPTIONS), livePort: DEFAULT_LIVE_PORT };
    }
  }
  function pageOf(node) {
    let current = node;
    while (current && current.type !== "PAGE") current = current.parent;
    return current && current.type === "PAGE" ? current : null;
  }
  function copyBackground(source, target) {
    var _a, _b, _c, _d, _e, _f;
    const s = source;
    if (s.fills !== void 0 && typeof s.fills !== "symbol") target.fills = s.fills;
    if (s.strokes !== void 0) target.strokes = s.strokes;
    if (s.strokeAlign !== void 0) target.strokeAlign = s.strokeAlign;
    if (typeof s.strokeWeight === "number") {
      target.strokeWeight = s.strokeWeight;
    } else if (typeof s.strokeTopWeight === "number") {
      target.strokeTopWeight = s.strokeTopWeight;
      target.strokeRightWeight = (_a = s.strokeRightWeight) != null ? _a : 0;
      target.strokeBottomWeight = (_b = s.strokeBottomWeight) != null ? _b : 0;
      target.strokeLeftWeight = (_c = s.strokeLeftWeight) != null ? _c : 0;
    }
    if (s.dashPattern !== void 0) target.dashPattern = s.dashPattern;
    if (typeof s.topLeftRadius === "number") {
      target.topLeftRadius = s.topLeftRadius;
      target.topRightRadius = (_d = s.topRightRadius) != null ? _d : 0;
      target.bottomRightRadius = (_e = s.bottomRightRadius) != null ? _e : 0;
      target.bottomLeftRadius = (_f = s.bottomLeftRadius) != null ? _f : 0;
    } else if (typeof s.cornerRadius === "number") {
      target.cornerRadius = s.cornerRadius;
    }
    if (typeof s.cornerSmoothing === "number") target.cornerSmoothing = s.cornerSmoothing;
    if (s.effects !== void 0) target.effects = s.effects;
  }
  var host = {
    async styleName(id) {
      const style = await figma.getStyleByIdAsync(id);
      return style == null ? void 0 : style.name;
    },
    async mainComponent(node) {
      return node.getMainComponentAsync();
    },
    async exportPng(node, scale) {
      var _a;
      const wasVisible = node.visible;
      if (!wasVisible) node.visible = true;
      try {
        const bytes = await node.exportAsync({ format: "PNG", constraint: { type: "SCALE", value: scale } });
        const absBounds = (_a = "absoluteRenderBounds" in node ? node.absoluteRenderBounds : null) != null ? _a : node.absoluteBoundingBox;
        return { bytes, absBounds };
      } finally {
        if (!wasVisible) node.visible = false;
      }
    },
    async exportBackgroundPng(node, scale) {
      var _a;
      const rect = figma.createRectangle();
      try {
        rect.name = "__widgetify_background__";
        rect.resize(Math.max(node.width, 0.01), Math.max(node.height, 0.01));
        copyBackground(node, rect);
        const bytes = await rect.exportAsync({ format: "PNG", constraint: { type: "SCALE", value: scale } });
        const t = rect.absoluteTransform;
        const rb = (_a = rect.absoluteRenderBounds) != null ? _a : { x: t[0][2], y: t[1][2], width: node.width, height: node.height };
        return { bytes, bounds: { x: rb.x - t[0][2], y: rb.y - t[1][2], width: rb.width, height: rb.height } };
      } finally {
        rect.remove();
      }
    },
    async ensurePageLoaded(node) {
      const page = pageOf(node);
      if (page && page !== figma.currentPage) await page.loadAsync();
    },
    progress(done, total, label) {
      post({ type: "progress", done, total, label });
    }
  };
  var exporting = false;
  async function runExport(options, purpose) {
    var _a;
    if (exporting) return;
    const selection = describeSelection();
    if (!selection.ok) {
      post({ type: "export-error", message: (_a = selection.reason) != null ? _a : "Nothing to export." });
      return;
    }
    exporting = true;
    try {
      const root = figma.currentPage.selection[0];
      const exporter = new FrameExporter(options, host);
      const result = await exporter.run(root, { fileName: figma.root.name, pageName: figma.currentPage.name });
      const safeName = result.bundle.source.frameName.replace(/[^\w-]+/g, "_") || "Screen";
      post({
        type: "bundle",
        purpose,
        fileName: `${safeName}.figmasync.zip`,
        frameName: result.bundle.source.frameName,
        frameId: result.bundle.source.frameId,
        json: result.json,
        files: result.files,
        warnings: result.bundle.warnings,
        stats: result.stats
      });
    } catch (e) {
      post({ type: "export-error", message: e instanceof Error ? e.message : String(e) });
    } finally {
      exporting = false;
    }
  }
  figma.ui.onmessage = async (msg) => {
    switch (msg.type) {
      case "ready":
        post({ type: "init", prefs: await loadPrefs(), selection: describeSelection() });
        break;
      case "export":
        await runExport(msg.options, msg.purpose);
        break;
      case "save-prefs":
        await figma.clientStorage.setAsync(PREFS_KEY, msg.prefs);
        break;
      case "notify":
        figma.notify(msg.text, { error: msg.error === true });
        break;
    }
  };
  figma.on("selectionchange", () => post({ type: "selection", selection: describeSelection() }));
})();
