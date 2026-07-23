// Browser port of the circuit-synthesis compiler (Phases 1–7 + prompt/bind).
// Runs entirely client-side so it can host on Vercel static.

const PartType = {
  Unknown: 0, Resistor: 1, Capacitor: 2, Inductor: 3,
  Diode: 4, Mosfet: 5, IC: 6,
};

const PartTypeName = {
  [PartType.Unknown]: "Unknown",
  [PartType.Resistor]: "Resistor",
  [PartType.Capacitor]: "Capacitor",
  [PartType.Inductor]: "Inductor",
  [PartType.Diode]: "Diode",
  [PartType.Mosfet]: "Mosfet",
  [PartType.IC]: "IC",
};

function designatorPrefix(t) {
  return ({ 1: "R", 2: "C", 3: "L", 4: "D", 5: "Q", 6: "U" })[t] || "X";
}

function buildBuckTopology(withController) {
  const t = {
    name: withController ? "buck_converter_controlled" : "buck_converter",
    nodes: ["VIN", "SW", "VOUT", "GND", "DRIVE"],
    components: [
      { role: "HS_SWITCH", type: PartType.Mosfet, quantity: 1,
        connections: [["D", "VIN"], ["S", "SW"], ["G", "DRIVE"]] },
      { role: "FREEWHEEL_DIODE", type: PartType.Diode, quantity: 1,
        connections: [["K", "SW"], ["A", "GND"]] },
      { role: "OUTPUT_INDUCTOR", type: PartType.Inductor, quantity: 1,
        connections: [["1", "SW"], ["2", "VOUT"]] },
      { role: "INPUT_CAP", type: PartType.Capacitor, quantity: 1,
        connections: [["1", "VIN"], ["2", "GND"]] },
      { role: "OUTPUT_CAP", type: PartType.Capacitor, quantity: 1,
        connections: [["1", "VOUT"], ["2", "GND"]] },
      { role: "LOAD", type: PartType.Resistor, quantity: 1,
        connections: [["1", "VOUT"], ["2", "GND"]] },
    ],
  };
  if (withController) {
    t.components.push({
      role: "CONTROLLER", type: PartType.IC, quantity: 1,
      connections: [["VIN", "VIN"], ["GND", "GND"], ["SW", "SW"], ["FB", "VOUT"], ["GATE", "DRIVE"]],
    });
  }
  return t;
}

function instantiate(topology) {
  const design = { topology: topology.name, nodes: [], components: [] };
  const addNode = (name) => {
    let i = design.nodes.findIndex((n) => n.name === name);
    if (i >= 0) return i;
    design.nodes.push({ name });
    return design.nodes.length - 1;
  };
  topology.nodes.forEach(addNode);
  const counters = {};
  for (const tc of topology.components) {
    const qty = Math.max(1, tc.quantity || 1);
    for (let i = 0; i < qty; i++) {
      const prefix = designatorPrefix(tc.type);
      counters[prefix] = (counters[prefix] || 0) + 1;
      const c = {
        ref: prefix + counters[prefix],
        role: tc.role,
        type: tc.type,
        value: 0,
        mpn: "",
        package: "",
        pins: tc.connections.map(([pin, node]) => ({ name: pin, node: addNode(node) })),
      };
      design.components.push(c);
    }
  }
  return design;
}

function parseBuckPrompt(text) {
  const lower = text.toLowerCase();
  if (!/(buck|step-down|step down)/.test(lower)) {
    return { ok: false, error: "no recognized topology in prompt" };
  }
  const re = /([0-9]+(?:\.[0-9]+)?)\s*(mhz|khz|hz|ma|mv|kv|uh|nh|uf|nf|pf|v|a|w)\b/gi;
  const volts = [], amps = [], freqs = [];
  let m;
  while ((m = re.exec(lower))) {
    const val = parseFloat(m[1]);
    const unit = m[2].toLowerCase();
    if (unit === "v") volts.push(val);
    else if (unit === "mv") volts.push(val / 1000);
    else if (unit === "kv") volts.push(val * 1000);
    else if (unit === "a") amps.push(val);
    else if (unit === "ma") amps.push(val / 1000);
    else if (unit === "hz") freqs.push(val);
    else if (unit === "khz") freqs.push(val * 1e3);
    else if (unit === "mhz") freqs.push(val * 1e6);
  }
  if (volts.length < 2) return { ok: false, error: "need two voltages (Vin and Vout)" };
  if (!amps.length) return { ok: false, error: "need an output current" };
  let [vin, vout] = volts;
  if (vin < vout) [vin, vout] = [vout, vin];
  return {
    ok: true,
    topology: "buck_converter",
    spec: {
      vin_v: vin,
      vout_v: vout,
      iout_a: amps[0],
      fsw_hz: freqs[0] || 500e3,
      ripple_current_ratio: 0.30,
    },
  };
}

function sizeBuck(spec, design) {
  if (spec.vin_v <= 0 || spec.vout_v <= 0 || spec.iout_a <= 0) {
    return { ok: false, error: "Vin, Vout, Iout must all be positive" };
  }
  if (spec.vout_v >= spec.vin_v) {
    return { ok: false, error: "Buck requires Vout < Vin (step-down)" };
  }
  const dVout = 0.01 * spec.vout_v;
  const dVin = 0.02 * spec.vin_v;
  const duty = spec.vout_v / spec.vin_v;
  const delta_il = spec.ripple_current_ratio * spec.iout_a;
  const il_peak = spec.iout_a + delta_il / 2;
  const l_henries = (spec.vout_v * (spec.vin_v - spec.vout_v)) /
    (spec.vin_v * spec.fsw_hz * delta_il);
  const cout_farads = delta_il / (8 * spec.fsw_hz * dVout);
  const cin_farads = (spec.iout_a * duty * (1 - duty)) / (spec.fsw_hz * dVin);
  const rload_ohms = spec.vout_v / spec.iout_a;
  const r = { ok: true, duty, delta_il, il_peak, l_henries, cout_farads, cin_farads, rload_ohms };
  if (design) {
    for (const c of design.components) {
      if (c.role === "OUTPUT_INDUCTOR") c.value = r.l_henries;
      else if (c.role === "OUTPUT_CAP") c.value = r.cout_farads;
      else if (c.role === "INPUT_CAP") c.value = r.cin_farads;
      else if (c.role === "LOAD") c.value = r.rload_ohms;
    }
  }
  return r;
}

function buildMockCatalog() {
  return [
    { mpn: "IND-6R8-3A0", type: PartType.Inductor, value: 6.8e-6, package: "1210", v_rating: 0, i_rating: 3 },
    { mpn: "IND-6R8-5A0", type: PartType.Inductor, value: 6.8e-6, package: "1210", v_rating: 0, i_rating: 5 },
    { mpn: "IND-4R7-6A0", type: PartType.Inductor, value: 4.7e-6, package: "1210", v_rating: 0, i_rating: 6 },
    { mpn: "CAP-4U7-6V3", type: PartType.Capacitor, value: 4.7e-6, package: "0805", v_rating: 6.3, i_rating: 0 },
    { mpn: "CAP-4U7-25V", type: PartType.Capacitor, value: 4.7e-6, package: "0805", v_rating: 25, i_rating: 0 },
    { mpn: "CAP-6U8-10V", type: PartType.Capacitor, value: 6.8e-6, package: "0805", v_rating: 10, i_rating: 0 },
    { mpn: "CAP-6U8-16V", type: PartType.Capacitor, value: 6.8e-6, package: "0805", v_rating: 16, i_rating: 0 },
    { mpn: "CAP-10U-25V", type: PartType.Capacitor, value: 10e-6, package: "1206", v_rating: 25, i_rating: 0 },
    { mpn: "CAP-2U2-25V", type: PartType.Capacitor, value: 2.2e-6, package: "0603", v_rating: 25, i_rating: 0 },
    { mpn: "CAP-0U47-50V", type: PartType.Capacitor, value: 0.47e-6, package: "0402", v_rating: 50, i_rating: 0 },
    { mpn: "MOS-20V-4A", type: PartType.Mosfet, value: 0, package: "SOT-23", v_rating: 20, i_rating: 4 },
    { mpn: "MOS-30V-8A", type: PartType.Mosfet, value: 0, package: "PowerPAK", v_rating: 30, i_rating: 8 },
    { mpn: "MOS-60V-10A", type: PartType.Mosfet, value: 0, package: "DPAK", v_rating: 60, i_rating: 10 },
    { mpn: "MOS-100V-20A", type: PartType.Mosfet, value: 0, package: "D2PAK", v_rating: 100, i_rating: 20 },
    { mpn: "IND-10U-8A0", type: PartType.Inductor, value: 10e-6, package: "1210", v_rating: 0, i_rating: 8 },
    { mpn: "CAP-22U-25V", type: PartType.Capacitor, value: 22e-6, package: "1206", v_rating: 25, i_rating: 0 },
    { mpn: "CAP-4U7-63V", type: PartType.Capacitor, value: 4.7e-6, package: "1206", v_rating: 63, i_rating: 0 },
    { mpn: "DIO-20V-2A", type: PartType.Diode, value: 0, package: "SOD-123", v_rating: 20, i_rating: 2 },
    { mpn: "DIO-40V-5A", type: PartType.Diode, value: 0, package: "SMA", v_rating: 40, i_rating: 5 },
    { mpn: "DIO-100V-5A", type: PartType.Diode, value: 0, package: "SMC", v_rating: 100, i_rating: 5 },
    { mpn: "BUCK-CTRL-1", type: PartType.IC, value: 0, package: "SOIC-8", v_rating: 100, i_rating: 0 },
  ];
}

function findCandidates(catalog, q) {
  let out = catalog.filter((p) => p.type === q.type);
  if (q.has_value_target) {
    const lo = q.value_target * (1 - q.value_tol);
    const hi = q.value_target * (1 + q.value_tol);
    out = out.filter((p) => p.value >= lo && p.value <= hi);
  }
  out.sort((a, b) => {
    if (q.has_value_target) {
      const da = Math.abs(a.value - q.value_target);
      const db = Math.abs(b.value - q.value_target);
      if (da !== db) return da - db;
    }
    if (a.i_rating !== b.i_rating) return a.i_rating - b.i_rating;
    return a.v_rating - b.v_rating;
  });
  return out;
}

function computeRequirements(spec, sizing) {
  return {
    OUTPUT_INDUCTOR: {
      needs_binding: true, has_value_target: true,
      value_target: sizing.l_henries, value_tol: 0.30,
      min_i_rating: sizing.il_peak * 1.2, min_v_rating: 0,
    },
    OUTPUT_CAP: {
      needs_binding: true, has_value_target: true,
      value_target: sizing.cout_farads, value_tol: 0.40,
      min_v_rating: spec.vout_v / 0.5, min_i_rating: 0,
    },
    INPUT_CAP: {
      needs_binding: true, has_value_target: true,
      value_target: sizing.cin_farads, value_tol: 0.40,
      min_v_rating: spec.vin_v / 0.8, min_i_rating: 0,
    },
    HS_SWITCH: {
      needs_binding: true, has_value_target: false,
      min_v_rating: spec.vin_v * 1.5, min_i_rating: sizing.il_peak * 1.5,
    },
    FREEWHEEL_DIODE: {
      needs_binding: true, has_value_target: false,
      min_v_rating: spec.vin_v * 1.3, min_i_rating: spec.iout_a,
    },
    LOAD: { needs_binding: false },
    CONTROLLER: {
      needs_binding: true, has_value_target: false,
      min_v_rating: spec.vin_v * 1.3, min_i_rating: 0,
    },
  };
}

function verify(part, req) {
  if (req.has_value_target) {
    const lo = req.value_target * (1 - req.value_tol);
    const hi = req.value_target * (1 + req.value_tol);
    if (part.value < lo || part.value > hi) return { ok: false, reason: "value out of tolerance" };
  }
  if (req.min_v_rating > 0 && part.v_rating < req.min_v_rating) {
    return { ok: false, reason: `insufficient voltage rating (${part.v_rating} < ${req.min_v_rating})` };
  }
  if (req.min_i_rating > 0 && part.i_rating < req.min_i_rating) {
    return { ok: false, reason: `insufficient current rating (${part.i_rating} < ${req.min_i_rating})` };
  }
  return { ok: true, reason: "" };
}

function bindDesign(design, requirements, catalog) {
  const outcomes = [];
  let all_bound = true;
  for (const c of design.components) {
    const req = requirements[c.role];
    const out = { ref: c.ref, role: c.role, skipped: false, bound: false, chosen_mpn: "", rejected: [], failure: "" };
    if (!req || !req.needs_binding) {
      out.skipped = true;
      outcomes.push(out);
      continue;
    }
    const candidates = findCandidates(catalog, {
      type: c.type,
      has_value_target: !!req.has_value_target,
      value_target: req.value_target || 0,
      value_tol: req.value_tol || 0.3,
    });
    for (const cand of candidates) {
      const v = verify(cand, req);
      if (v.ok) {
        c.mpn = cand.mpn;
        c.value = cand.value;
        c.package = cand.package;
        out.bound = true;
        out.chosen_mpn = cand.mpn;
        break;
      }
      out.rejected.push({ mpn: cand.mpn, reason: v.reason });
    }
    if (!out.bound) {
      out.failure = "no catalog part satisfied requirements";
      all_bound = false;
    }
    outcomes.push(out);
  }
  return { outcomes, all_bound };
}

function runErc(design) {
  const errors = [], warnings = [];
  const terminals = design.nodes.map(() => 0);
  for (const c of design.components) {
    for (const p of c.pins) {
      if (p.node < 0 || p.node >= design.nodes.length) errors.push(`Unconnected pin ${c.ref}.${p.name}`);
      else terminals[p.node]++;
    }
  }
  if (!design.nodes.some((n) => n.name === "GND")) errors.push("No ground (GND) reference net");
  terminals.forEach((n, i) => {
    if (n === 0) warnings.push(`Net '${design.nodes[i].name}' has no terminals`);
    else if (n === 1) warnings.push(`Net '${design.nodes[i].name}' is floating (single terminal)`);
  });
  return { errors, warnings, ok: errors.length === 0 };
}

function placeDesign(design) {
  const origin = 25.4, spacing = 50.8, pitch = 2.54, cols = 3;
  return design.components.map((c, i) => {
    const n = c.pins.length;
    return {
      ref: c.ref,
      type: c.type,
      at: { x: origin + (i % cols) * spacing, y: origin + Math.floor(i / cols) * spacing },
      pin_offsets: c.pins.map((p, j) => ({
        name: p.name,
        x: 0,
        y: ((n - 1) / 2 - j) * pitch,
      })),
    };
  });
}

function libNameFor(t) {
  return ({ 1: "synth:R", 2: "synth:C", 3: "synth:L", 4: "synth:D", 5: "synth:Q", 6: "synth:U" })[t] || "synth:X";
}

function toKicadSch(design, placement) {
  const byRef = Object.fromEntries(placement.map((p) => [p.ref, p]));
  const proto = {};
  for (const p of placement) if (!(p.type in proto)) proto[p.type] = p;

  let o = `(kicad_sch\n\t(version 20250114)\n\t(generator "parser_new_synth")\n\t(generator_version "1.0")\n\t(lib_symbols\n`;
  for (const [type, pc] of Object.entries(proto)) {
    const lib = libNameFor(+type);
    o += `\t\t(symbol "${lib}"\n\t\t\t(symbol "${lib}_0_1"\n`;
    for (const pn of pc.pin_offsets) {
      o += `\t\t\t\t(pin passive line (at ${pn.x} ${pn.y} 0) (length 1.27) (name "~") (number "${pn.name}"))\n`;
    }
    o += `\t\t\t)\n\t\t)\n`;
  }
  o += `\t)\n`;

  for (const c of design.components) {
    const pc = byRef[c.ref];
    const x = pc?.at.x ?? 0, y = pc?.at.y ?? 0;
    const value = c.mpn || String(c.value);
    o += `\t(symbol\n\t\t(lib_id "${libNameFor(c.type)}")\n\t\t(at ${x} ${y} 0)\n\t\t(unit 1)\n`;
    o += `\t\t(property "Reference" "${c.ref}" (at ${x} ${y - 5.08} 0))\n`;
    o += `\t\t(property "Value" "${value}" (at ${x} ${y + 5.08} 0))\n`;
    if (c.package) o += `\t\t(property "Footprint" "${c.package}" (at ${x} ${y} 0))\n`;
    o += `\t)\n`;
  }

  for (const c of design.components) {
    const pc = byRef[c.ref];
    if (!pc) continue;
    c.pins.forEach((pin, j) => {
      if (j >= pc.pin_offsets.length) return;
      const net = design.nodes[pin.node]?.name;
      if (!net) return;
      const wx = pc.at.x + pc.pin_offsets[j].x;
      const wy = pc.at.y + pc.pin_offsets[j].y;
      o += `\t(label "${net}" (at ${wx} ${wy} 0))\n`;
    });
  }
  o += `)\n`;
  return o;
}

function toNetlist(design) {
  const terminals = design.nodes.map(() => []);
  for (const c of design.components) {
    for (const p of c.pins) {
      if (p.node >= 0 && p.node < terminals.length) terminals[p.node].push(`${c.ref}.${p.name}`);
    }
  }
  return {
    topology: design.topology,
    nodes: design.nodes.map((n) => n.name),
    components: design.components.map((c) => ({
      ref: c.ref,
      role: c.role,
      type: PartTypeName[c.type] || "Unknown",
      value: c.value,
      mpn: c.mpn || null,
      package: c.package,
      pins: c.pins.map((p) => ({
        name: p.name,
        node: design.nodes[p.node]?.name ?? null,
      })),
    })),
    nets: design.nodes.map((n, i) => ({ name: n.name, pins: terminals[i] })),
  };
}

/** Full pipeline: prompt → verified IR → schematic text. */
export function synthesize(prompt, withController = false) {
  const parsed = parseBuckPrompt(prompt);
  if (!parsed.ok) return { ok: false, error: `prompt parse failed: ${parsed.error}` };

  const design = instantiate(buildBuckTopology(withController));
  const sizing = sizeBuck(parsed.spec, design);
  if (!sizing.ok) return { ok: false, error: `sizing failed: ${sizing.error}` };

  const req = computeRequirements(parsed.spec, sizing);
  const bind = bindDesign(design, req, buildMockCatalog());
  if (!bind.all_bound) {
    return { ok: false, error: "part binding failed (a required component had no valid part)", bind };
  }

  const erc = runErc(design);
  if (!erc.ok) return { ok: false, error: "verified IR failed ERC", erc };

  const placement = placeDesign(design);
  const kicad = toKicadSch(design, placement);
  const netlist = toNetlist(design);

  let bind_trace = "";
  for (const o of bind.outcomes) {
    bind_trace += `${o.ref} (${o.role}): `;
    if (o.skipped) bind_trace += "SKIPPED\n";
    else if (o.bound) bind_trace += `BOUND -> ${o.chosen_mpn}\n`;
    else bind_trace += "FAILED\n";
    for (const rc of o.rejected) bind_trace += `    rejected ${rc.mpn} : ${rc.reason}\n`;
  }

  let nets_text = "";
  for (const n of netlist.nets) {
    nets_text += `${n.name}: ${n.pins.join(", ")}\n`;
  }

  return {
    ok: true,
    topology: design.topology,
    kicad_path: "design.kicad_sch",
    erc_ok: erc.ok,
    erc,
    spec: parsed.spec,
    sizing,
    nodes: design.nodes.map((n) => n.name),
    components: design.components.map((c) => ({
      ref: c.ref,
      role: c.role,
      mpn: c.mpn || null,
      value: c.value,
      package: c.package,
    })),
    bind_trace,
    nets_text,
    netlist,
    kicad,
  };
}
