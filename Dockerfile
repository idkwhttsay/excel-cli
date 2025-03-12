# Start from a base image with C build tools
FROM gcc:latest AS builder

# Create and set working directory
WORKDIR /app

# Copy all source files and headers
COPY src/ /app/src/
COPY nob.c nob.h /app/
COPY csv/ /app/csv/

# Build the application
# Assuming the main executable should be built from main.c and nob.c
RUN gcc -o excel-cli src/main.c
RUN chmod +x excel-cli

# Use a smaller base image for the final image
FROM ubuntu:latest

# Install any runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy only the necessary files from the builder stage
COPY --from=builder /app/excel-cli /app/excel-cli
COPY --from=builder /app/csv/ /app/csv/

# Ensure the executable has proper permissions
RUN chmod +x /app/excel-cli

# Verify the file exists (will fail during build if not)
RUN ls -la /app/excel-cli

# Set the command to run your application when the container starts
CMD ["/app/excel-cli", "/app/csv/input.csv"]