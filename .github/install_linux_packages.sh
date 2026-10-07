#!/usr/bin/env bash
set -euo pipefail

# The hosted runner's Azure HTTP mirror stalled both Linux jobs during package
# acquisition. Its official HTTPS fallback responded in the same run. Keep the
# signed Ubuntu repositories and package verification; change only that mirror.
if [[ -f /etc/apt/apt-mirrors.txt ]]; then
    sudo sed -i 's|http://azure\.archive\.ubuntu\.com/ubuntu|https://archive.ubuntu.com/ubuntu|g' /etc/apt/apt-mirrors.txt
fi

apt_options=(
    -o Acquire::Retries=2
    -o Acquire::http::Timeout=20
    -o Acquire::https::Timeout=20
)
sudo apt-get "${apt_options[@]}" -o APT::Update::Error-Mode=any update
sudo apt-get "${apt_options[@]}" install -y "$@"
