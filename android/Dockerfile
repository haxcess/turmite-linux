# Published Java/Android SDK environment; project-specific tools are added below.
FROM thyrlian/android-sdk:10.0

ENV ANDROID_HOME=/opt/android-sdk \
    GRADLE_HOME=/opt/turmite-gradle/gradle-8.11.1
ENV PATH=${GRADLE_HOME}/bin:${ANDROID_HOME}/cmdline-tools/tools/bin:${ANDROID_HOME}/platform-tools:${PATH}

# Android's Linux NDK host tools require x86-64; launcher selects linux/amd64.
RUN test "$(uname -m)" = x86_64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl unzip rsync util-linux libstdc++6 zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Override the base image's Gradle with the version required by our AGP.
RUN curl -fL --retry 3 https://services.gradle.org/distributions/gradle-8.11.1-bin.zip -o /tmp/gradle.zip \
    && echo 'f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6  /tmp/gradle.zip' | sha256sum -c - \
    && mkdir -p /opt/turmite-gradle \
    && unzip -q /tmp/gradle.zip -d /opt/turmite-gradle \
    && rm /tmp/gradle.zip

# Building this image accepts the Android SDK package licenses.
RUN yes | sdkmanager --licenses > /tmp/sdk-licenses.log
RUN sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'platform-tools' \
        'ndk;28.0.13004108' 'cmake;3.22.1' \
    && chmod -R a+rX ${ANDROID_HOME} \
    && rm /tmp/sdk-licenses.log

ENV HOME=/cache/home \
    GRADLE_USER_HOME=/cache/gradle \
    ANDROID_USER_HOME=/cache/home/.android \
    JAVA_TOOL_OPTIONS=-Duser.home=/cache/home
COPY container-entrypoint.sh /usr/local/bin/turmite-build
ENTRYPOINT ["/bin/sh", "/usr/local/bin/turmite-build"]

# Do not inherit the SDK image's supervisord command as Gradle arguments.
CMD []
