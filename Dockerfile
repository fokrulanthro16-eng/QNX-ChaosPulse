# ==============================================================================
# QNX-ChaosPulse Production Multi-Stage Containerfile
# Stage 1: Asset Validation & Linting
# Stage 2: Hardened Non-Root Alpine Runner
# ==============================================================================

# ------------------------------------------------------------------------------
# Stage 1: Builder
# ------------------------------------------------------------------------------
FROM node:20-alpine AS builder

WORKDIR /app

# Copy application manifests and source code
COPY package.json ./
COPY host/ ./host/
COPY docs/ ./docs/

# Lint host telemetry server and validate audit report schema
RUN node --check host/server.js && \
    node -e "const fs = require('fs'); JSON.parse(fs.readFileSync('docs/QNX-ChaosPulse-Audit-Report.json', 'utf8')); console.log('Audit Report Validated');"

# ------------------------------------------------------------------------------
# Stage 2: Production Runtime
# ------------------------------------------------------------------------------
FROM node:20-alpine AS runner

LABEL maintainer="FOKRUL ISLAM" \
      project="QNX-ChaosPulse" \
      standard="ISO 26262 ASIL-D / IEC 61508 SIL-3" \
      version="1.0.0"

WORKDIR /app

# Set hardened production environment
ENV NODE_ENV=production \
    PORT=3000 \
    TELEMETRY_RATE_HZ=21.0 \
    TARGET_LINK=MOCK_FALLBACK

# Install curl for reliable healthcheck probes
RUN apk --no-cache add curl

# Copy runtime assets from builder stage
COPY --from=builder /app/package.json ./package.json
COPY --from=builder /app/host ./host
COPY --from=builder /app/docs ./docs

# Bundle target C microkernel sources for runtime reference and mock physics
COPY target/ ./target/

# Switch to non-root system user for security compliance
USER node

# Expose Web Cockpit UI & WebSocket Port
EXPOSE 3000

# Container Healthcheck against HTTP Cockpit endpoint
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:3000/ || exit 1

# Start Cockpit Telemetry Gateway & Engine
CMD ["node", "host/server.js"]
