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
                
                cat > ai_review.py <<'PY'
                import json
                import urllib.request
                
                OLLAMA_URL = "http://192.168.11.129:11434/api/generate"
                MODEL = "qwen2.5-coder:7b"
                
                def read_file(path):
                    try:
                        with open(path, "r", errors="ignore") as f:
                            return f.read()
                    except Exception:
                        return ""
                
                cppcheck = read_file("reports/cppcheck-report.txt")
                security = read_file("reports/security-patterns.txt")
                
                prompt = f"""
                You are a senior embedded Linux C security reviewer.
                
                Create a professional Markdown report.
                
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
                {cppcheck[:12000]}
                
                Security Scan:
                {security[:12000]}
                """
                
                payload = {
                    "model": MODEL,
                    "prompt": prompt,
                    "stream": False
                }
                
                output = ""
                
                try:
                    req = urllib.request.Request(
                        OLLAMA_URL,
                        data=json.dumps(payload).encode("utf-8"),
                        headers={"Content-Type": "application/json"}
                    )
                
                    with urllib.request.urlopen(req, timeout=300) as response:
                        result = json.loads(response.read().decode("utf-8"))
                
                    output = result.get("response", "")
                
                except Exception as e:
                    output = f"""
                # AI Code and Security Review Report
                
                ## AI Review Failed
                
                Error:
                
                {e}
                """
                
                with open("reports/ai-code-security-review.md", "w") as f:
                    f.write(output)
                
                print(output)
                PY
                
                python3 ai_review.py
                
                echo "===== REPORT FILES ====="
                ls -lh reports
                
                echo "===== AI REPORT ====="
                cat reports/ai-code-security-review.md || true
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
