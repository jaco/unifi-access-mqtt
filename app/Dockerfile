# syntax=docker/dockerfile:1.17

# Build stage for backend
FROM golang:1.25-alpine AS backend-builder

WORKDIR /app

# Install dependencies
RUN apk add --no-cache git

# Copy go mod files
COPY go.mod go.sum ./
RUN go mod download

# Copy source code
COPY . .

# Build the binary
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o unifi-access-mqtt .

# Final stage
FROM gcr.io/distroless/static:nonroot

WORKDIR /

# Copy the binary from builder stage
COPY --from=backend-builder /app/unifi-access-mqtt /unifi-access-mqtt

# Create a non-root user (distroless already provides this)
USER nonroot:nonroot

ENTRYPOINT ["/unifi-access-mqtt", "/var/lib/unifi-access-mqtt/config.json"]
