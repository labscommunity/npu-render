$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\compiled-turbo-build-context-v1.json"
if(Test-Path $out){throw 'Context evidence exists'}
$items=@(foreach($name in @('native-routing-v45','ptl-compiled-turbo-v1')){
 $cache=@([IO.File]::ReadAllLines("$r/builds/$name/CMakeCache.txt")|Where-Object {$_ -match '^(CMAKE_GENERATOR|CMAKE_CXX_FLAGS_RELEASE|CMAKE_VS_PLATFORM|CMAKE_CONFIGURATION_TYPES|CMAKE_GENERATOR_TOOLSET|CMAKE_CACHE_MAJOR_VERSION|CMAKE_CACHE_MINOR_VERSION|CMAKE_MAKE_PROGRAM)[:_]'}|ForEach-Object {[string]$_})
 $build=@([IO.File]::ReadAllLines("$r/vm/build-$name.log")|Where-Object {$_ -match 'compiler identification|Windows SDK|MSBuild version'}|Select-Object -First 3|ForEach-Object {[string]$_})
 [ordered]@{name=$name;cache=$cache;build=$build;cache_file_sha256=(Get-FileHash "$r/builds/$name/CMakeCache.txt").Hash.ToLower();build_log_sha256=(Get-FileHash "$r/vm/build-$name.log").Hash.ToLower()}
})
if(($items[0].cache -join "`n") -ne ($items[1].cache -join "`n") -or ($items[0].build -join "`n") -ne ($items[1].build -join "`n")){throw 'Recorded toolchain or release configuration differs'}
[ordered]@{passed=$true;observed_utc=[DateTime]::UtcNow.ToString('o');recorded_compiler_sdk_generator_and_release_flags_match=$true;items=$items;scope='Recorded build configuration; not an independent performance result'}|ConvertTo-Json -Depth 6|Set-Content $out
