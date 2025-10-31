$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'ISOBOOT' -or $_.FileSystemLabel -eq 'ISOEFI' }
foreach ($vol in $volumes) {
    $part = Get-Partition | Where-Object { $_.AccessPaths -contains $vol.Path }
    if ($part) {
        Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber -AccessPath $vol.Path -Confirm:$false
        Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false
    }
}
$systemPartition = Get-Partition | Where-Object { $_.DriveLetter -eq 'C' }
if ($systemPartition) {
    $supportedSize = Get-PartitionSupportedSize -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber
    Resize-Partition -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber -Size $supportedSize.SizeMax -Confirm:$false
}
