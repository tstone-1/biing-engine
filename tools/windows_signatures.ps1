# Machine-readable native verification for sign_windows.py. No signing or login.
param(
    [Parameter(Mandatory)][string]$PathsFile,
    [string]$SignTool
)
$ErrorActionPreference = 'Stop'
if (-not $SignTool) {
    $found = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($found) { $SignTool = $found.Source }
    else {
        $SignTool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter signtool.exe |
            Where-Object FullName -like '*\x64\*' | Sort-Object FullName |
            Select-Object -Last 1 -ExpandProperty FullName
    }
}
if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool -PathType Leaf)) {
    throw 'Windows SDK signtool.exe is required'
}
$rows = @(foreach ($path in (Get-Content -Raw -LiteralPath $PathsFile | ConvertFrom-Json)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    $verified = $false
    if ($signature.Status -eq 'Valid') {
        & $SignTool verify /pa /all $path *> $null
        $verified = $LASTEXITCODE -eq 0
    }
    $name = ''; $thumbprint = ''
    if ($signature.SignerCertificate) {
        $name = $signature.SignerCertificate.GetNameInfo([System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
        $thumbprint = $signature.SignerCertificate.Thumbprint
    }
    [pscustomobject]@{
        path = $path; status = $signature.Status.ToString(); verified = $verified
        signer = $name; thumbprint = $thumbprint; timestamp = [bool]$signature.TimeStamperCertificate
    }
})
ConvertTo-Json -InputObject $rows -Depth 4 -Compress
