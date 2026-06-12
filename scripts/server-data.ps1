param (
    [ValidateScript( { Test-Path $_ })]
    [string]$in = (Join-Path $PSScriptRoot .. "build"),

    [ValidateScript( { Test-Path $_ })]
    [string]$out = (Join-Path $PSScriptRoot .. "dist" "server")
)

xcopy "$in\3ddata\ai\*.aip" "$out\3ddata\ai\" /S /Y /D
xcopy "$in\3ddata\ai\*.stb" "$out\3ddata\ai\" /S /Y /D
xcopy "$in\3ddata\effect\*.zmo" "$out\3ddata\effect\" /S /Y /D
xcopy "$in\3ddata\maps\*.him" "$out\3ddata\maps\" /S /Y /D
xcopy "$in\3ddata\maps\*.mov" "$out\3ddata\maps\" /S /Y /D
xcopy "$in\3ddata\maps\*.ifo" "$out\3ddata\maps\" /S /Y /D
xcopy "$in\3ddata\maps\*.zon" "$out\3ddata\maps\" /S /Y /D
xcopy "$in\3ddata\mdata\*.zmo" "$out\3ddata\mdata\" /S /Y /D
xcopy "$in\3ddata\motion\*.zmo" "$out\3ddata\motion\" /S /Y /D
xcopy "$in\3ddata\npc\*.chr" "$out\3ddata\npc\" /S /Y /D
xcopy "$in\3ddata\npc\*.zmo" "$out\3ddata\npc\" /S /Y /D
xcopy "$in\3ddata\npc\*.zsc" "$out\3ddata\npc\" /S /Y /D
xcopy "$in\3ddata\pat\*.zmo" "$out\3ddata\pat\" /S /Y /D
xcopy "$in\3ddata\questdata\*.qsd" "$out\3ddata\questdata\" /S /Y /D
xcopy "$in\3ddata\questdata\*.stb" "$out\3ddata\questdata\" /S /Y /D
xcopy "$in\3ddata\stb\*.stb" "$out\3ddata\stb\" /S /Y /D
xcopy "$in\3ddata\stb\*.stl" "$out\language\" /S /Y /D
xcopy "$in\3ddata\stb\list_quest_s.stl" "$out\3ddata\questdata\" /S /Y /D