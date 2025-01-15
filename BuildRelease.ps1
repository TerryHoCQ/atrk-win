<#
��;�����ɷ����档
#>

$ErrorActionPreference = "Stop"

try{

$ts = (Get-Date).ToString("yyyyMMdd-HHmmss")
$scriptDir = split-path -parent $MyInvocation.MyCommand.Definition
$releaseDir = join-path $scriptDir ("atrk-win-$ts")

$dirs = @(
"$releaseDir")

Write-Host "�����ļ�..."

foreach ($i in $dirs) {
   mkdir $i | Out-Null
}

Copy-Item "$scriptDir\ports\Release\ports.exe" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\ps\Release\ps.exe" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\ps\x64\Release\ps_x64.exe" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\rawdir\bin\Release\rawdir.exe" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\atrk-win.bat" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\README.md" -Recurse -Destination "$releaseDir\"
Copy-Item "$scriptDir\LICENSE" -Recurse -Destination "$releaseDir\"

$compress = @{
  Path = "$releaseDir\*"
  DestinationPath = "atrk-win-$ts.zip"
}
Compress-Archive @compress
Write-Host "���"

}
catch{
  $error[0].Exception
  $_ |select -expandproperty invocationinfo
}

Write-Host "��������˳�..."
cmd /c pause | Out-Null
