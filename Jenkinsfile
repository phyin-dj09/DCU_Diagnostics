pipeline {
    agent {
        docker {
            label 'dcu-diagnostics'
            image 'ubuntu:20.04'
            args '-u root -v /mnt/cicd-artifacts:/artifacts'
        }
    }

    stages {
        stage('Install Dependencies') {
            steps {
                sh '''
                    apt-get update
                    DEBIAN_FRONTEND=noninteractive apt-get install -y \
                        gcc \
                        make \
                        cmake \
                        git \
                        libssl-dev \
                        libjansson-dev \
                        curl \
                        cppcheck \
                        python3
                '''
            }
        }

        stage('Build and Install Paho MQTT C') {
        steps {
            sh '''
                rm -rf /tmp/paho.mqtt.c
    
                for i in 1 2 3; do
                    git clone --depth 1 --branch v1.3.14 https://github.com/eclipse-paho/paho.mqtt.c.git /tmp/paho.mqtt.c && break
                    echo "Git clone failed. Retrying..."
                    rm -rf /tmp/paho.mqtt.c
                    sleep 5
                done
    
                cd /tmp/paho.mqtt.c
    
                cmake -B build -S . \
                    -DPAHO_WITH_SSL=TRUE \
                    -DPAHO_BUILD_SHARED=TRUE \
                    -DPAHO_BUILD_STATIC=FALSE \
                    -DPAHO_ENABLE_TESTING=FALSE
    
                cmake --build build
                cmake --install build
    
                ldconfig
            '''
            }
        }

        stage('Static Code Analysis') {
        steps {
            sh '''
                mkdir -p reports
    
                cppcheck \
                    --enable=all \
                    --inconclusive \
                    --std=c99 \
                    --force \
                    --suppress=missingIncludeSystem \
                    --template=gcc \
                    Gateway_plugin 2> reports/cppcheck-report.txt || true
    
                cat reports/cppcheck-report.txt
            '''
            }
        }

        stage('Basic Security Pattern Scan') {
        steps {
            sh '''
                mkdir -p reports
    
                grep -RInE "strcpy|strcat|sprintf|gets\\(|system\\(|popen\\(|scanf\\(" Gateway_plugin \
                    > reports/security-patterns.txt || true
    
                cat reports/security-patterns.txt || true
            '''
            }
        }

        stage('Local AI Code and Security Review') {
            steps {
                sh '''
                    mkdir -p reports

                    echo "===== RUNNING AI REVIEW ====="
                    python3 - <<'PY'
import json
import os
import urllib.request


OLLAMA_URL = os.environ.get(
    "OLLAMA_URL",
    "http://192.168.11.129:11434/api/generate",
)
MODEL = os.environ.get("OLLAMA_MODEL", "qwen2.5-coder:7b")
REPORT_PATH = "reports/ai-code-security-review.md"
TIMEOUT_SECONDS = int(os.environ.get("OLLAMA_TIMEOUT_SECONDS", "900"))


def read_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as handle:
            return handle.read()
    except OSError:
        return ""


def failure_report(message):
    return f"""# AI Code and Security Review Report

## AI Review Failed

Model: `{MODEL}`

Endpoint: `{OLLAMA_URL}`

Error:

```text
{message}
```
"""


def build_prompt(cppcheck, security):
    return f"""
You are a senior embedded Linux C security reviewer.

Create a professional Markdown report.

IMPORTANT RULES:
- Do not invent people names.
- Do not invent company names.
- Do not invent authors.
- Do not invent report signatures.
- Do not add 'Prepared By'.
- Do not add consultant names.
- Do not add cloud vendor names.
- Only use information present in the scan results.
- If information is unavailable, write "Not Available".

Project: DCU_Diagnostics / Gateway_plugin

Report Sections:
1. Executive Summary
2. High Priority Issues
3. Medium Priority Issues
4. Low Priority Issues
5. File-wise Findings
6. Recommended Fixes
7. Developer Action Items
8. Build Decision

Do not invent issues.
Only use the scan results below.

Cppcheck Report:
{cppcheck[:4000]}

Security Scan:
{security[:4000]}
"""


def run_review():
    cppcheck = read_file("reports/cppcheck-report.txt")
    security = read_file("reports/security-patterns.txt")

    payload = {
        "model": MODEL,
        "prompt": build_prompt(cppcheck, security),
        "stream": False,
    }

    req = urllib.request.Request(
        OLLAMA_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )

    with urllib.request.urlopen(req, timeout=TIMEOUT_SECONDS) as response:
        result = json.loads(response.read().decode("utf-8"))

    output = result.get("response", "").strip()
    if not output:
        return failure_report("The Ollama API returned an empty response.")

    return output + chr(10)


os.makedirs("reports", exist_ok=True)

try:
    output = run_review()
except Exception as exc:
    output = failure_report(str(exc))

with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    handle.write(output)

print(output)
PY

                    echo "===== REPORT FILES ====="
                    ls -lh reports

                    echo "===== AI REPORT ====="
                    cat reports/ai-code-security-review.md
                '''
            }
        }

        stage('Archive Review Reports') {
            steps {
                archiveArtifacts artifacts: 'reports/*', fingerprint: true
            }
        }

        stage('Build Gateway Plugin') {
            steps {
                sh '''
                    cd Gateway_plugin
                    make clean
                    make
                '''
            }
        }

        stage('Verify Binary') {
            steps {
                sh '''
                    ls -lh Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY
                    ldd Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY
                '''
            }
        }

        stage('Archive Artifact') {
            steps {
                archiveArtifacts artifacts: 'Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY', fingerprint: true
            }
        }

        stage('Agent Check') {
        steps {
            sh '''
                hostname
                whoami
            '''
            }
        }    
        
        stage('Publish to Local Artifact Directory') {
        steps {
            sh '''
                ARTIFACT_DIR=/artifacts/DCU_Diagnostics/latest
                REPORT_DIR=${ARTIFACT_DIR}/reports
    
                mkdir -p ${ARTIFACT_DIR}
    
                echo "Workspace reports before publishing:"
                ls -lh reports || true
    
                rm -rf ${REPORT_DIR}
                mkdir -p ${REPORT_DIR}
    
                if [ -d reports ]; then
                    cp -av reports/. ${REPORT_DIR}/
                else
                    echo "No reports directory found"
                fi
    
                cp Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY ${ARTIFACT_DIR}/
                chmod +x ${ARTIFACT_DIR}/PHYSOFTWARE_DCU_GATEWAY
    
                cd ${ARTIFACT_DIR}
                sha256sum PHYSOFTWARE_DCU_GATEWAY > PHYSOFTWARE_DCU_GATEWAY.sha256
    
                echo "${BUILD_NUMBER}" > build_number.txt
                date > build_time.txt
    
                echo "Published artifact directory:"
                ls -lh ${ARTIFACT_DIR}
    
                echo "Published reports directory:"
                ls -lh ${REPORT_DIR}
            '''
            }
        }
    }

    post {
        success {
            echo 'Build completed successfully for Ubuntu 20.04.'
        }

        failure {
            echo 'Build failed. Check Console Output.'
        }
    }
}
