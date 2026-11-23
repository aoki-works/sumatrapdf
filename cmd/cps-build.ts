// CPS Lab: command-line build wrapper (not in upstream).
// 使い方:   bun cmd/cps-build.ts [-dbg|-rel] [-64|-32] [-target <name>] [-rebuild] [-premake]
import { existsSync, mkdirSync, readFileSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { detectVisualStudio } from "./util.ts";

const args = process.argv.slice(2);
const has = (f: string) => args.includes(f);
const valueOf = (f: string, def: string) => {
  const i = args.indexOf(f);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};

if (has("-h") || has("-help")) {
  console.log("usage: bun cmd/cps-build.ts [-dbg|-rel] [-64|-32] [-target <name>] [-rebuild] [-premake]");
  process.exit(0);
}

const config = has("-rel") ? "Release" : "Debug";
const platform = has("-32") ? "Win32" : "x64";
const target = valueOf("-target", "SumatraPDF") + (has("-rebuild") ? ":Rebuild" : "");
const outDir = join("out", (config === "Release" ? "rel" : "dbg") + (platform === "x64" ? "64" : "32"));
const logPath = resolve(join("out", "cps-build.log"));
mkdirSync("out", { recursive: true });

async function run(cmd: string, cmdArgs: string[]): Promise<number> {
  console.log(`> ${[cmd.split("\\").pop(), ...cmdArgs].join(" ")}`);
  const proc = Bun.spawn([cmd, ...cmdArgs], { stdout: "inherit", stderr: "inherit", stdin: "ignore" });
  return await proc.exited;
}

if (has("-premake")) {
  const premake = join("bin", "premake5.exe");
  for (const action of ["vs2022", "vs2026"]) {
    const rc = await run(premake, [action]);
    if (rc !== 0) {
      console.error(`CPS-BUILD: premake ${action} FAILED (exit ${rc})`);
      process.exit(rc);
    }
  }
}

const { msbuildPath } = detectVisualStudio();
const started = Date.now();
const rc = await run(msbuildPath, [
  join("vs2022", "SumatraPDF.sln"),
  `/t:${target}`,
  `/p:Configuration=${config};Platform=${platform}`,
  "/m",
  "/nologo",
  "/v:minimal",
  `/flp:logfile=${logPath};verbosity=normal;encoding=utf-8`,
]);
const secs = Math.round((Date.now() - started) / 1000);

if (rc !== 0) {
  console.error(`\nCPS-BUILD: FAILED (exit ${rc}, ${secs}s) ${config}|${platform}. log: ${logPath}`);
  if (existsSync(logPath)) {
    const errs = readFileSync(logPath, "utf-8")
      .split(/\r?\n/)
      .filter((l) => /\b(error|fatal error) [A-Z]+\d+\b/.test(l));
    const uniq = [...new Set(errs.map((l) => l.trim()))];
    console.error(`--- ${uniq.length} error line(s) (first 60) ---`);
    for (const l of uniq.slice(0, 60)) console.error(l);
  }
  process.exit(rc);
}

const exe = join(outDir, "SumatraPDF.exe");
if (existsSync(exe)) {
  const st = statSync(exe);
  console.log(`\nCPS-BUILD: OK (${secs}s) ${config}|${platform} -> ${exe} (${st.size} bytes, ${st.mtime.toISOString()})`);
} else {
  console.log(`\nCPS-BUILD: OK (${secs}s) ${config}|${platform}, but ${exe} not found (target: ${target})`);
}
