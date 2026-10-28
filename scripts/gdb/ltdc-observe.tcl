# Run after init with a matching diagnostic ELF; no reset, halt or writes.
if {[mrw $vg_diag_addr] != 0x4c544443 ||
    [mrw [expr {$vg_diag_addr + 4}]] != 1} {
    error "LTDC diagnostic marker mismatch; check the flashed ELF"
}
set vg_fu0 [mrw [expr {$vg_diag_addr + 8}]]
set vg_te0 [mrw [expr {$vg_diag_addr + 12}]]
set vg_rr0 [mrw [expr {$vg_diag_addr + 28}]]
echo "vg-ltdc registers (IER/ISR, CFBAR/pitch/lines, NVIC ISER2):"
echo [mdw 0x50001034 2]
echo [mdw 0x500010ac 3]
echo [mdw 0xe000e108 1]
for {set vg_n 0} {$vg_n < $vg_diag_samples} {incr vg_n} {
    set vg_fu [mrw [expr {$vg_diag_addr + 8}]]
    set vg_te [mrw [expr {$vg_diag_addr + 12}]]
    set vg_rr [mrw [expr {$vg_diag_addr + 28}]]
    set vg_last_isr [mrw [expr {$vg_diag_addr + 16}]]
    set vg_last_fb [mrw [expr {$vg_diag_addr + 20}]]
    set vg_last_pos [mrw [expr {$vg_diag_addr + 24}]]
    echo "vg-ltdc sample=$vg_n fifo=$vg_fu transfer=$vg_te reload=$vg_rr last_isr=$vg_last_isr last_fb=$vg_last_fb last_pos=$vg_last_pos"
    sleep $vg_diag_ms
}
set vg_fu [mrw [expr {$vg_diag_addr + 8}]]
set vg_te [mrw [expr {$vg_diag_addr + 12}]]
set vg_rr [mrw [expr {$vg_diag_addr + 28}]]
echo "vg-ltdc result fifo_delta=[expr {$vg_fu - $vg_fu0}] transfer_delta=[expr {$vg_te - $vg_te0}] reload_delta=[expr {$vg_rr - $vg_rr0}]"
