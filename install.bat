certutil -addstore Root testcert.cer
certutil -addstore TrustedPublisher testcert.cer
bcdedit -set testsigning on
pnputil /add-driver AppleStudioDisplayXDRAmbientLightUSBSensorFix.inf /install
pnputil /add-driver AppleStudioDisplayXDRAmbientLightOrientationHIDSensorsNull.inf /install
