#!/usr/bin/env bash
# Prints the path of the OVMF firmware code ("code") or variable store ("vars") file: the one next to the installed QEMU (Homebrew's edk2-*.fd), else the usual
# Linux package locations (ovmf, edk2-ovmf). Usage: tools/find_ovmf.sh code|vars [qemu-prefix]. When nothing exists it prints the Homebrew path anyway, so the
# caller's "no such file" names something sensible.
kind="${1:?usage: find_ovmf.sh code|vars [qemu-prefix]}"
prefix="${2:-/usr/local}"
if [ "$kind" = code ]; then
    cands=("$prefix/share/qemu/edk2-x86_64-code.fd" /usr/share/qemu/edk2-x86_64-code.fd /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.fd)
else
    cands=("$prefix/share/qemu/edk2-i386-vars.fd" /usr/share/qemu/edk2-i386-vars.fd /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/edk2/ovmf/OVMF_VARS.fd /usr/share/edk2-ovmf/x64/OVMF_VARS.fd)
fi
for c in "${cands[@]}"; do
    if [ -f "$c" ]; then echo "$c"; exit 0; fi
done
echo "${cands[0]}"
