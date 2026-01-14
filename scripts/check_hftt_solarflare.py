#!/usr/bin/env python3
import argparse
import ipaddress
import json
import re
import subprocess
import sys
from typing import Dict, List, Tuple


DEFAULT_HOSTS = ["hftt0", "hftt1", "hftt2", "hftt3"]
DEFAULT_USER = "user"
DEFAULT_SWITCH_HOST = ""
DEFAULT_SWITCH_USER = "admin"
DEFAULT_EXPECTED_PORT = "Et47"
DEFAULT_MD_MCAST_IP = "239.0.0.1"
DEFAULT_SNAPSHOT_MCAST_IP = "239.0.0.3"
DEFAULT_MCAST_PORT = 12345


def run(cmd: List[str], timeout: int = 10) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)


def ssh_cmd(
    host: str, user: str, remote_cmd: str, timeout: int = 10, tty: bool = False
) -> subprocess.CompletedProcess:
    cmd = [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=5",
    ]
    if tty:
        cmd.append("-t")
    cmd.extend(
        [
        f"{user}@{host}",
        remote_cmd,
        ]
    )
    return run(cmd, timeout=timeout)


def solarflare_ifaces_cmd() -> str:
    # Detect interfaces using the "sfc" driver (Solarflare).
    return (
        "for i in /sys/class/net/*; do "
        "iface=${i##*/}; "
        "if [ -e \"$i/device/driver\" ]; then "
        "drv=$(basename \"$(readlink -f \"$i/device/driver\")\"); "
        "if [ \"$drv\" = \"sfc\" ]; then echo \"$iface\"; fi; "
        "fi; "
        "done"
    )


def parse_ip_addrs(output: str) -> List[str]:
    addrs: List[str] = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 4:
            addrs.append(parts[3])
    return addrs


def normalize_mac(raw: str) -> str:
    return re.sub(r"[^0-9a-fA-F]", "", raw).lower()


def local_solarflare_ifaces() -> Dict[str, str]:
    detect = run(["bash", "-lc", solarflare_ifaces_cmd()])
    if detect.returncode != 0:
        return {}
    ifaces = [line.strip() for line in detect.stdout.splitlines() if line.strip()]
    macs: Dict[str, str] = {}
    for iface in ifaces:
        try:
            mac = run(["cat", f"/sys/class/net/{iface}/address"])
            if mac.returncode == 0:
                macs[iface] = mac.stdout.strip()
        except FileNotFoundError:
            continue
    return macs


def local_ip_map() -> Dict[str, List[str]]:
    ip_out = run(["ip", "-o", "-4", "addr", "show"])
    iface_ips: Dict[str, List[str]] = {}
    for line in ip_out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4:
            iface = parts[1]
            ip = parts[3]
            iface_ips.setdefault(iface, []).append(ip)
    return iface_ips


def find_iface_for_ip(target_ip: str, iface_ips: Dict[str, List[str]]) -> str:
    for iface, ips in iface_ips.items():
        for ip in ips:
            if ip.split("/")[0] == target_ip:
                return iface
    return ""


def ip_from_start(start_ip: str, offset: int) -> str:
    base = ipaddress.ip_address(start_ip)
    return str(base + offset)


def netplan_yaml_for_ifaces(ifaces: List[str], addresses: List[str]) -> str:
    lines = [
        "network:",
        "  version: 2",
        "  ethernets:",
    ]
    for iface, addr in zip(ifaces, addresses):
        lines.extend(
            [
                f"    {iface}:",
                "      dhcp4: no",
                "      addresses:",
                f"        - {addr}/24",
            ]
        )
    return "\n".join(lines) + "\n"


def switch_mac_port(switch_host: str, switch_user: str, mac: str) -> str:
    if not switch_host:
        return ""
    mac_norm = normalize_mac(mac)
    cmd = (
        "enable\n"
        f"show mac address-table address {mac}\n"
    )
    out = subprocess.run(
        [
            "ssh",
            "-o",
            "BatchMode=yes",
            "-o",
            "ConnectTimeout=5",
            "-t",
            f"{switch_user}@{switch_host}",
            cmd,
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    if out.returncode != 0:
        return ""
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4:
            line_mac = normalize_mac(parts[1])
            if line_mac == mac_norm:
                return parts[3]
    return ""


def ssh_sudo_write_file(
    host: str, user: str, remote_path: str, content: str, interactive: bool
) -> subprocess.CompletedProcess:
    cmd = [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "ConnectTimeout=5",
    ]
    if interactive:
        cmd.append("-t")
    cmd.extend(
        [
        f"{user}@{host}",
        f"sudo {'-n ' if not interactive else ''}tee {remote_path} >/dev/null",
        ]
    )
    return subprocess.run(cmd, text=True, input=content, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def configure_iface_runtime(host: str, user: str, iface: str, ip_addr: str) -> subprocess.CompletedProcess:
    # Only touch the target interface to avoid bouncing management NICs.
    cmd = (
        f"sudo -n ip addr flush dev {iface} && "
        f"sudo -n ip addr add {ip_addr}/24 dev {iface} && "
        f"sudo -n ip link set {iface} up"
    )
    return ssh_cmd(host, user, cmd, timeout=10)


def check_mcast_on_host(
    host: str,
    user: str,
    ifaces: List[str],
    mcast_ip: str,
    mcast_port: int,
    timeout_sec: int,
) -> Tuple[bool, str]:
    for iface in ifaces:
        cmd = (
            f"sudo -n timeout {timeout_sec} "
            f"tcpdump -n -i {iface} udp and host {mcast_ip} and port {mcast_port} -c 1"
            " >/dev/null 2>&1"
        )
        result = ssh_cmd(host, user, cmd, timeout=timeout_sec + 2)
        if result.returncode == 0:
            return True, iface
    return False, ""


def configure_host(
    host: str,
    user: str,
    start_ip: str,
    host_index: int,
    interactive_sudo: bool,
    apply_netplan: bool,
) -> Tuple[bool, str]:
    detect = ssh_cmd(host, user, solarflare_ifaces_cmd())
    if detect.returncode != 0:
        return False, detect.stderr.strip() or "ssh failed"

    ifaces = sorted(line.strip() for line in detect.stdout.splitlines() if line.strip())
    if len(ifaces) != 2:
        return False, f"expected 2 Solarflare interfaces, found {len(ifaces)}"

    ip1 = ip_from_start(start_ip, host_index * 2)
    ip2 = ip_from_start(start_ip, host_index * 2 + 1)
    netplan = netplan_yaml_for_ifaces(ifaces, [ip1, ip2])

    write = ssh_sudo_write_file(
        host,
        user,
        "/etc/netplan/60-solarflare-static.yaml",
        netplan,
        interactive=interactive_sudo,
    )
    if write.returncode != 0:
        return False, write.stderr.strip() or "sudo write failed"

    if apply_netplan:
        try:
            apply = ssh_cmd(
                host,
                user,
                f"sudo {'-n ' if not interactive_sudo else ''}netplan apply",
                timeout=30,
                tty=interactive_sudo,
            )
        except subprocess.TimeoutExpired:
            return False, "netplan apply timed out"
        if apply.returncode != 0:
            return False, apply.stderr.strip() or apply.stdout.strip() or "netplan apply failed"
    else:
        for iface, ip_addr in zip(ifaces, [ip1, ip2]):
            apply = configure_iface_runtime(host, user, iface, ip_addr)
            if apply.returncode != 0:
                return False, apply.stderr.strip() or apply.stdout.strip() or "interface update failed"

    return True, f"{ifaces[0]}={ip1}, {ifaces[1]}={ip2}"


def get_local_target_ip() -> str:
    detect = run(["bash", "-lc", solarflare_ifaces_cmd()])
    if detect.returncode != 0:
        return ""
    ifaces = [line.strip() for line in detect.stdout.splitlines() if line.strip()]
    ips: List[str] = []
    for iface in ifaces:
        ip_out = run(["ip", "-o", "-4", "addr", "show", "dev", iface])
        ips.extend(parse_ip_addrs(ip_out.stdout))
    if len(ips) == 1:
        return ips[0].split("/")[0]
    return ""


def check_host(host: str, user: str, target_ip: str) -> Tuple[bool, Dict[str, object]]:
    result: Dict[str, object] = {"host": host, "solarflare_ifaces": {}, "ping_ok": False}

    detect = ssh_cmd(host, user, solarflare_ifaces_cmd())
    if detect.returncode != 0:
        result["error"] = detect.stderr.strip() or "ssh failed"
        return False, result

    ifaces = [line.strip() for line in detect.stdout.splitlines() if line.strip()]
    if not ifaces:
        result["error"] = "no Solarflare interfaces detected (driver sfc)"
        return False, result

    for iface in ifaces:
        ip_out = ssh_cmd(host, user, f"ip -o -4 addr show dev {iface}")
        if ip_out.returncode == 0:
            result["solarflare_ifaces"][iface] = parse_ip_addrs(ip_out.stdout)
        else:
            result["solarflare_ifaces"][iface] = []

    has_ip = any(result["solarflare_ifaces"].get(iface) for iface in ifaces)
    if not has_ip:
        result["error"] = "no IPv4 addresses on Solarflare interfaces"
        return False, result

    ping = ssh_cmd(host, user, f"ping -c 2 -W 1 {target_ip}")
    result["ping_ok"] = ping.returncode == 0
    if not result["ping_ok"]:
        result["error"] = ping.stderr.strip() or ping.stdout.strip() or "ping failed"
        return False, result

    return True, result


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Solarflare IPs and ping target from HFTT hosts.")
    parser.add_argument("--hosts", nargs="+", default=DEFAULT_HOSTS, help="Hosts to check.")
    parser.add_argument("--user", default=DEFAULT_USER, help="SSH user.")
    parser.add_argument("--target-ip", default="", help="Target IP to ping (host on switch port 47).")
    parser.add_argument("--json", action="store_true", help="Emit JSON output.")
    parser.add_argument("--configure", action="store_true", help="Configure Solarflare IPs via netplan.")
    parser.add_argument("--start-ip", default="192.168.1.10", help="Start IP for host assignments.")
    parser.add_argument("--skip-check", action="store_true", help="Skip ping/IP verification.")
    parser.add_argument(
        "--interactive-sudo",
        action="store_true",
        help="Allow sudo password prompts over SSH (requires TTY).",
    )
    parser.add_argument(
        "--netplan-apply",
        action="store_true",
        help="Run 'netplan apply' after writing config (may bounce management NICs).",
    )
    parser.add_argument("--check-mcast", action="store_true", help="Check multicast reception on clients.")
    parser.add_argument("--md-mcast-ip", default=DEFAULT_MD_MCAST_IP, help="Market data multicast IP.")
    parser.add_argument("--snapshot-mcast-ip", default=DEFAULT_SNAPSHOT_MCAST_IP, help="Snapshot multicast IP.")
    parser.add_argument("--mcast-port", type=int, default=DEFAULT_MCAST_PORT, help="Multicast port.")
    parser.add_argument("--mcast-timeout", type=int, default=3, help="Tcpdump timeout seconds.")
    parser.add_argument("--switch-host", default=DEFAULT_SWITCH_HOST, help="Arista switch host for warnings.")
    parser.add_argument("--switch-user", default=DEFAULT_SWITCH_USER, help="Arista switch user for warnings.")
    parser.add_argument("--expected-port", default=DEFAULT_EXPECTED_PORT, help="Expected switch port for target IP.")
    args = parser.parse_args()

    if args.configure:
        for idx, host in enumerate(args.hosts):
            ok, msg = configure_host(
                host,
                args.user,
                args.start_ip,
                idx,
                args.interactive_sudo,
                args.netplan_apply,
            )
            status = "OK" if ok else "FAIL"
            print(f"{host}: {status} - {msg}")

    if args.skip_check:
        return 0

    target_ip = args.target_ip or get_local_target_ip()
    if not target_ip:
        print("Unable to determine target IP. Provide --target-ip.", file=sys.stderr)
        return 2 if not args.configure else 1

    warnings: List[str] = []
    iface_ips = local_ip_map()
    target_iface = find_iface_for_ip(target_ip, iface_ips)
    local_sfc = local_solarflare_ifaces()
    if target_iface and target_iface in local_sfc:
        mac = local_sfc[target_iface]
        port = switch_mac_port(args.switch_host, args.switch_user, mac)
        if not args.switch_host:
            warnings.append(
                f"Target IP {target_ip} is on {target_iface} (MAC {mac}); "
                "set --switch-host to validate switch port."
            )
        elif not port:
            warnings.append(
                f"Target IP {target_ip} is on {target_iface} (MAC {mac}), "
                "but it was not found in the switch MAC table."
            )
        elif port != args.expected_port:
            warnings.append(
                f"Target IP {target_ip} is on {target_iface} (MAC {mac}) "
                f"learned on {port}, expected {args.expected_port}."
            )
    elif target_iface:
        warnings.append(f"Target IP {target_ip} is on {target_iface}, not a Solarflare interface.")
    else:
        warnings.append(f"Target IP {target_ip} not found on this host.")

    all_ok = True
    results: List[Dict[str, object]] = []
    for host in args.hosts:
        ok, res = check_host(host, args.user, target_ip)
        if args.check_mcast:
            detect = ssh_cmd(host, args.user, solarflare_ifaces_cmd())
            if detect.returncode == 0:
                ifaces = [line.strip() for line in detect.stdout.splitlines() if line.strip()]
                md_ok, md_iface = check_mcast_on_host(
                    host,
                    args.user,
                    ifaces,
                    args.md_mcast_ip,
                    args.mcast_port,
                    args.mcast_timeout,
                )
                snap_ok, snap_iface = check_mcast_on_host(
                    host,
                    args.user,
                    ifaces,
                    args.snapshot_mcast_ip,
                    args.mcast_port,
                    args.mcast_timeout,
                )
                res["mcast_md_ok"] = md_ok
                res["mcast_md_iface"] = md_iface
                res["mcast_snapshot_ok"] = snap_ok
                res["mcast_snapshot_iface"] = snap_iface
                if not md_ok:
                    res["error"] = res.get("error", "")
                    res["error"] = (res["error"] + "; " if res["error"] else "") + "market data multicast not seen"
                if not snap_ok:
                    res["error"] = res.get("error", "")
                    res["error"] = (res["error"] + "; " if res["error"] else "") + "snapshot multicast not seen"
                ok = ok and md_ok and snap_ok
        results.append(res)
        all_ok = all_ok and ok

    if args.json:
        print(json.dumps({"target_ip": target_ip, "warnings": warnings, "results": results}, indent=2))
    else:
        print(f"Target IP: {target_ip}")
        for warning in warnings:
            print(f"WARNING: {warning}")
        for res in results:
            host = res["host"]
            if "error" in res:
                print(f"{host}: FAIL - {res['error']}")
            else:
                iface_summaries = []
                for iface, addrs in res["solarflare_ifaces"].items():
                    iface_summaries.append(f"{iface}={','.join(addrs) if addrs else 'no-ip'}")
                status = f"{host}: OK - {', '.join(iface_summaries)} - ping ok"
                if args.check_mcast:
                    md_iface = res.get("mcast_md_iface") or "n/a"
                    snap_iface = res.get("mcast_snapshot_iface") or "n/a"
                    status += f" - md on {md_iface}, snapshot on {snap_iface}"
                print(status)

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
