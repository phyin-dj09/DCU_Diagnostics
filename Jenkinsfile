pipeline {
    agent any

    stages {
        stage('Checkout') {
            steps {
                echo 'Checking out source code...'
                checkout scm
            }
        }

        stage('Install Dependencies') {
            steps {
                echo 'Installing required build dependencies...'
                sh '''
                    apt-get update
                    apt-get install -y gcc make libjansson-dev libpaho-mqtt-dev
                '''
            }
        }

        stage('Build Gateway Plugin') {
            steps {
                echo 'Building project using Makefile...'
                sh '''
                    cd Gateway_plugin
                    make clean
                    make
                '''
            }
        }

        stage('Verify Binary') {
            steps {
                echo 'Checking generated binary...'
                sh '''
                    ls -lh Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY
                '''
            }
        }

        stage('Archive Artifact') {
            steps {
                echo 'Saving build output in Jenkins...'
                archiveArtifacts artifacts: 'Gateway_plugin/PHYSOFTWARE_DCU_GATEWAY', fingerprint: true
            }
        }
    }

    post {
        success {
            echo 'Build completed successfully.'
        }

        failure {
            echo 'Build failed. Check Console Output.'
        }
    }
}
