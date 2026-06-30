FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tests
COPY test.sh test_thermal.py thermal_oracle.hpp thermal_oracle_impl.cpp /tests/

CMD ["bash", "/tests/test.sh"]
