#!/usr/bin/env bash
set -euo pipefail
if [[ "${EUID}" -ne 0 ]]; then
  echo "Execute com sudo: sudo ./instalar_linux.sh"
  exit 1
fi
SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
apt-get update
apt-get install -y python3
if ! id ludo-team >/dev/null 2>&1; then
  useradd --system --home /var/lib/ludo-team-server --shell /usr/sbin/nologin ludo-team
fi
install -d -o ludo-team -g ludo-team /opt/ludo-team-server /var/lib/ludo-team-server
install -m 0644 "$SOURCE_DIR/ludo_server.py" /opt/ludo-team-server/ludo_server.py
install -m 0755 "$SOURCE_DIR/run_server.sh" /opt/ludo-team-server/run_server.sh
install -m 0644 "$SOURCE_DIR/ludo-team-server.service.example" /etc/systemd/system/ludo-team-server.service
if [[ ! -f /etc/ludo-team-server.env ]]; then
  install -m 0640 "$SOURCE_DIR/ludo-team-server.env.example" /etc/ludo-team-server.env
fi
chown root:ludo-team /etc/ludo-team-server.env
systemctl daemon-reload
systemctl enable ludo-team-server.service
cat <<'EOF'

LUDO Team Server instalado.

1. Edite /etc/ludo-team-server.env (nome e URL HTTPS pública).
2. Crie o primeiro administrador:
   sudo -u ludo-team python3 /opt/ludo-team-server/ludo_server.py \
     --data /var/lib/ludo-team-server --add-user SEU_USUARIO --role admin
3. Inicie:
   sudo systemctl start ludo-team-server
4. Configure um proxy HTTPS (exemplo: Caddyfile.example) apontando para 127.0.0.1:8787.

A porta 8787 deve continuar privada. Exponha somente HTTPS (normalmente 443).
EOF
