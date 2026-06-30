FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

CMD ["python3", "/app/tasks/thermal-pulse-solver-cpp/tests/sweeps/run_overresolve_gate.py", "--budget", "180"]
