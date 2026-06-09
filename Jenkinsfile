pipeline {
    agent {
        docker {
            image 'ubuntu:20.04'
            args '-u root'
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
                        libjansson-dev
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
        
        stage('Publish to Local Artifact Directory') {
        steps {
            sh '''
                ARTIFACT_DIR=/artifacts/DCU_Diagnostics/latest
    
                mkdir -p ${ARTIFACT_DIR}
    
                cp Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY ${ARTIFACT_DIR}/
                chmod +x ${ARTIFACT_DIR}/PHYSOFTWARE_DCU_GATEWAY
    
                sha256sum ${ARTIFACT_DIR}/PHYSOFTWARE_DCU_GATEWAY > ${ARTIFACT_DIR}/PHYSOFTWARE_DCU_GATEWAY.sha256
    
                echo "${BUILD_NUMBER}" > ${ARTIFACT_DIR}/build_number.txt
                date > ${ARTIFACT_DIR}/build_time.txt
    
                ls -lh ${ARTIFACT_DIR}
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
