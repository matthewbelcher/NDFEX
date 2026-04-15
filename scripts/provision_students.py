#!/usr/bin/env python3
import argparse
import csv
import getpass
import json
import os
import secrets
import shlex
import socket
import smtplib
import string
import subprocess
import sys
import textwrap
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


ALPHANUM = string.ascii_letters + string.digits


def generate_password(length: int) -> str:
    return "".join(secrets.choice(ALPHANUM) for _ in range(length))


def format_student_name(raw_name: str) -> str:
    parts = [part.strip() for part in raw_name.split(",", 1)]
    if len(parts) == 2 and parts[0] and parts[1]:
        return f"{parts[1]} {parts[0]}"
    return raw_name


def read_students(csv_path: Path) -> List[Dict[str, str]]:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        students = []
        for row in reader:
            login = (row.get("SIS Login ID") or "").strip()
            name = (row.get("Student") or "").strip()
            if not login or name.lower().startswith("points possible"):
                continue
            students.append({"login": login, "name": name})
    return students


def resolve_host_ips(hosts: Iterable[str], mapping_path: Path | None) -> Dict[str, str]:
    if mapping_path:
        with mapping_path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        return {str(host): str(ip) for host, ip in data.items()}

    resolved: Dict[str, str] = {}
    for host in hosts:
        result = subprocess.run(
            ["getent", "hosts", host],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0 or not result.stdout.strip():
            raise RuntimeError(f"Unable to resolve management IP for {host}.")
        resolved[host] = result.stdout.split()[0]
    return resolved


def run_ssh(host: str, ssh_user: str | None, ssh_options: List[str], command: str) -> None:
    target = f"{ssh_user}@{host}" if ssh_user else host
    cmd = ["ssh", *ssh_options, target, command]
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(
            f"SSH command failed on {host}: {result.stderr.strip() or result.stdout.strip()}"
        )


def provision_user(hosts: Iterable[str], login: str, password: str, ssh_user: str | None,
                   ssh_options: List[str], sudo_cmd: str, dry_run: bool) -> None:
    login_q = shlex.quote(login)
    password_q = shlex.quote(password)
    remote_cmd = (
        "set -euo pipefail; "
        f"if ! id -u {login_q} >/dev/null 2>&1; then {sudo_cmd} useradd -m -s /bin/bash {login_q}; fi; "
        f"printf '%s:%s\\n' {login_q} {password_q} | {sudo_cmd} chpasswd"
    )
    for host in hosts:
        if dry_run:
            continue
        run_ssh(host, ssh_user, ssh_options, remote_cmd)


def render_email(login: str, name: str, password: str, host_ips: Dict[str, str],
                 override_recipient: str | None = None, from_address: str | None = None) -> str:
    ip_lines = "\n".join(f"- {host}: {ip}" for host, ip in host_ips.items())
    display_name = format_student_name(name)
    recipient = override_recipient or f"{login}@nd.edu"
    from_header = f"From: {from_address}\nReply-To: {from_address}\n" if from_address else ""
    login_ip = host_ips.get("hftt0") or next(iter(host_ips.values()), "hftt0")
    return (
        f"To: {recipient}\n"
        "Subject: HFTT Homework 0 - lab account setup\n"
        f"{from_header}"
        "\n"
        f"Hi {display_name},\n"
        "\n"
        "Your HFTT lab account has been created. Use the following credentials to log in:\n"
        "\n"
        f"Login ID: {login}\n"
        f"Password: {password}\n"
        "\n"
        "Management IPs for the lab machines:\n"
        f"{ip_lines}\n"
        "\n"
        "Login instructions:\n"
        "1) From a terminal, connect using SSH:\n"
        f"   ssh {login}@{login_ip}\n"
        "   (or use any of the IPs above)\n"
        "2) Enter the password when prompted.\n"
        "\n"
        "Please change your password after first login.\n"

        "Your assignment for Homework 0 is to confirm that you can access the lab machines. If you have any problems email the instructor at mbelche2@nd.edu"
    )


def send_email(mail_command: str, email_body: str, dry_run: bool, recipient: str,
               smtp_server: str, smtp_port: int, smtp_user: str | None, smtp_password: str | None,
               from_address: str | None) -> None:
    if dry_run:
        return
    if smtp_server:
        if not smtp_user or not smtp_password:
            raise RuntimeError("SMTP user/password required for SMTP delivery.")
        from_addr = from_address or smtp_user
        with smtplib.SMTP(smtp_server, smtp_port) as client:
            client.starttls()
            client.login(smtp_user, smtp_password)
            client.sendmail(from_addr, [recipient], email_body)
        return
    cmd = shlex.split(mail_command)
    result = subprocess.run(cmd, text=True, input=email_body, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(f"Email send failed: {result.stderr.strip() or result.stdout.strip()}")


def write_passwords(output_path: Path, rows: List[Tuple[str, str, str]]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["login", "name", "password"])
        writer.writerows(rows)
    try:
        os.chmod(output_path, 0o600)
    except OSError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Provision HFTT student accounts and email credentials.")
    parser.add_argument("--csv", default="studentsids.csv", help="CSV with SIS Login IDs.")
    parser.add_argument("--hosts", nargs="+", default=["hftt0", "hftt1", "hftt2", "hftt3"])
    parser.add_argument("--ssh-user", default=getpass.getuser(), help="SSH user for provisioning.")
    parser.add_argument("--ssh-option", action="append", default=[], help="Extra SSH option (repeatable).")
    parser.add_argument(
        "--sudo-cmd",
        default="sudo -n",
        help="Sudo command to run on remote hosts (default: 'sudo -n').",
    )
    parser.add_argument(
        "--host-ip-map",
        help="JSON file mapping host->management IP. If omitted, resolves via getent.",
    )
    parser.add_argument(
        "--mail-command",
        default="sendmail -t",
        help="Command used to send mail if SMTP is not configured (default: 'sendmail -t').",
    )
    parser.add_argument("--password-length", type=int, default=10)
    parser.add_argument("--output", default="student_passwords.csv", help="Write passwords here.")
    parser.add_argument("--dry-run", action="store_true", help="Do not create users or send email.")
    parser.add_argument("--skip-email", action="store_true", help="Create users but skip email.")
    parser.add_argument("--limit", type=int, default=0, help="Limit to first N students.")
    parser.add_argument(
        "--test-recipient",
        default="",
        help="Send all emails to this address instead of {login}@nd.edu.",
    )
    parser.add_argument(
        "--from-address",
        default="mbelche2@nd.edu",
        help="From/Reply-To address for outbound emails.",
    )
    parser.add_argument("--smtp-server", default="smtp.gmail.com", help="SMTP server hostname.")
    parser.add_argument("--smtp-port", type=int, default=587, help="SMTP server port.")
    parser.add_argument(
        "--smtp-user",
        default="",
        help="SMTP username (defaults to from-address if unset).",
    )
    parser.add_argument(
        "--smtp-password-file",
        default=str(Path.home() / ".smtp_password"),
        help="Path to file containing SMTP password.",
    )
    args = parser.parse_args()

    csv_path = Path(args.csv)
    output_path = Path(args.output)
    host_ip_map = Path(args.host_ip_map) if args.host_ip_map else None

    students = read_students(csv_path)
    if not students:
        print("No students found in CSV.", file=sys.stderr)
        return 1

    try:
        host_ips = resolve_host_ips(args.hosts, host_ip_map)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    rows = []
    if args.limit and args.limit > 0:
        students = students[: args.limit]
    test_recipient = args.test_recipient.strip() or None
    from_address = args.from_address.strip() or None
    smtp_user = args.smtp_user.strip() or from_address
    smtp_password = os.environ.get("SMTP_PASSWORD")
    if not smtp_password:
        password_path = Path(args.smtp_password_file).expanduser()
        if password_path.exists():
            smtp_password = password_path.read_text(encoding="utf-8").strip()
    for student in students:
        login = student["login"]
        name = student["name"]
        password = generate_password(args.password_length)
        provision_user(
            args.hosts, login, password, args.ssh_user, args.ssh_option, args.sudo_cmd, args.dry_run
        )
        recipient = test_recipient or f"{login}@nd.edu"
        email_body = render_email(login, name, password, host_ips, test_recipient, from_address)
        if not args.skip_email:
            send_email(
                args.mail_command,
                email_body,
                args.dry_run,
                recipient,
                args.smtp_server,
                args.smtp_port,
                smtp_user,
                smtp_password,
                from_address,
            )
        rows.append((login, name, password))

    write_passwords(output_path, rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
