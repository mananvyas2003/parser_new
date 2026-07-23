# Builds and runs every synth phase/task test. Reports a pass/fail summary.
$ErrorActionPreference = "Continue"
$CXX = "g++"
$FLAGS = @("-std=c++17","-Wall","-Wextra","-I","synth")
New-Item -ItemType Directory -Force -Path build | Out-Null

# name => @(extra source files..., extra link flags...)
$tests = @(
  @{ n="ir_buck_test";           src=@("synth/tests/ir_buck_test.cpp");           extra=@() },
  @{ n="ir_json_test";           src=@("synth/tests/ir_json_test.cpp");           extra=@() },
  @{ n="topology_test";          src=@("synth/tests/topology_test.cpp");          extra=@() },
  @{ n="buck_sizing_test";       src=@("synth/tests/buck_sizing_test.cpp");       extra=@() },
  @{ n="part_binding_test";      src=@("synth/tests/part_binding_test.cpp");      extra=@() },
  @{ n="ir_graph_erc_test";      src=@("synth/tests/ir_graph_erc_test.cpp","graph..cpp","graph_search.cpp"); extra=@("-I",".") },
  @{ n="kicad_writer_test";      src=@("synth/tests/kicad_writer_test.cpp","lexer.cpp","parser.cpp","interpreter.cpp"); extra=@("-I",".") },
  @{ n="sqlite_catalog_test";    src=@("synth/tests/sqlite_catalog_test.cpp");    extra=@("-lsqlite3") },
  @{ n="prompt_parser_test";     src=@("synth/tests/prompt_parser_test.cpp");     extra=@() },
  @{ n="controller_topology_test"; src=@("synth/tests/controller_topology_test.cpp"); extra=@() },
  @{ n="pipeline_test";          src=@("synth/tests/pipeline_test.cpp");          extra=@() }
)

$results = @()
foreach ($t in $tests) {
  $exe = "build/$($t.n).exe"
  $args = $FLAGS + $t.src + @("-o",$exe) + $t.extra
  & $CXX @args 2> "build/$($t.n).build.log"
  # g++ warnings go to stderr but exit 0; rely on the real exit code, not $?.
  if ($LASTEXITCODE -ne 0) { $results += [pscustomobject]@{ Test=$t.n; Result="BUILD FAIL" }; continue }
  & ".\$exe" *> "build/$($t.n).log"
  if ($LASTEXITCODE -eq 0) { $results += [pscustomobject]@{ Test=$t.n; Result="PASS" } }
  else { $results += [pscustomobject]@{ Test=$t.n; Result="RUN FAIL ($LASTEXITCODE)" } }
}

Write-Host "`n================ TEST SUMMARY ================"
$results | ForEach-Object { "{0,-26} {1}" -f $_.Test, $_.Result } | Write-Host
$fail = ($results | Where-Object { $_.Result -ne "PASS" }).Count
Write-Host "=============================================="
if ($fail -eq 0) { Write-Host "ALL $($results.Count) TESTS PASSED" } else { Write-Host "$fail TEST(S) FAILED" }
