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
                    git clone https://github.com/eclipse-paho/paho.mqtt.c.git /tmp/paho.mqtt.c

                    cd /tmp/paho.mqtt.c
                    git checkout v1.3.14

                    cmake -Bbuild -H. \
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
