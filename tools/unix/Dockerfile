# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# FM-1 build environment. JieLi's toolchain is Linux x86-64 only, so this
# image is always linux/amd64 (Rosetta runs it on Apple silicon).
#   docker build --platform linux/amd64 -f tools/unix/Dockerfile -t fm1-build .
FROM --platform=linux/amd64 debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl xz-utils tar gzip python3 libicu72 \
    && rm -rf /var/lib/apt/lists/*
ARG PWSH_VERSION=7.4.6
ARG PWSH_SHA256=6f6015203c47806c5cc444c19d8ed019695e610fbd948154264bf9ca8e157561
RUN curl -fsSL --retry 3 -o /tmp/pwsh.tgz \
        "https://github.com/PowerShell/PowerShell/releases/download/v${PWSH_VERSION}/powershell-${PWSH_VERSION}-linux-x64.tar.gz" \
    && echo "${PWSH_SHA256}  /tmp/pwsh.tgz" | sha256sum -c - \
    && mkdir -p /opt/pwsh && tar -xzf /tmp/pwsh.tgz -C /opt/pwsh && rm /tmp/pwsh.tgz \
    && chmod +x /opt/pwsh/pwsh && ln -s /opt/pwsh/pwsh /usr/local/bin/pwsh
ARG SKIP_JIELI=0
ARG JIELI_TOOLCHAIN_SHA256=
COPY tools/unix/setup-toolchains.sh /opt/fm1/setup-toolchains.sh
RUN SKIP_JIELI=${SKIP_JIELI} JIELI_TOOLCHAIN_SHA256=${JIELI_TOOLCHAIN_SHA256} sh /opt/fm1/setup-toolchains.sh /opt/fm1
ENV FM1_TOOLS=/opt/fm1 DOTNET_CLI_TELEMETRY_OPTOUT=1 POWERSHELL_TELEMETRY_OPTOUT=1
WORKDIR /work
