certutil -addstore Root testcert.cer
certutil -addstore TrustedPublisher testcert.cer
pnputil /add-driver AppleStudioDisplayAmbientLightOrientationHIDSensorsNull.inf /install
