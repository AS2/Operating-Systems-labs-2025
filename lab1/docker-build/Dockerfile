# Single stage Alpine for building and running
FROM alpine:latest

# Install build dependencies
RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    bash

WORKDIR /workspace
COPY . .

# Make build script executable
RUN chmod +x build.sh

# Create non-root user (Alpine uses adduser)
RUN adduser -D -u 1000 builder && \
    chown -R builder:builder /workspace

USER builder

# Build the project
RUN ./build.sh

# Use the TARGET environment variable for entrypoint
ENTRYPOINT ["/bin/sh", "-c", "exec ./build/bin/${TARGET}"]