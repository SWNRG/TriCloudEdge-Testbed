**AWS_BACKEND_SETUP.md**

**AWS Backend Setup Guide**

Instructions for deploying the serverless AWS backend that receives images from the ESP32-S3 device, performs celebrity recognition, and communicates results via MQTT.

**Architecture Overview**

The backend workflow uses four core AWS services:

1.  **Amazon S3:** Stores raw images uploaded from the ESP32 device.
2.  **AWS Lambda:** Triggered by S3 uploads, this function runs Python code to convert the image and call the recognition service.
3.  **Amazon Rekognition:** Performs the AI-based facial analysis to identify celebrities.
4.  **AWS IoT Core:** Provides a secure MQTT broker for communication with the device.

**Critical Prerequisite: AWS Region**

This setup **must be deployed in an AWS Region where Amazon Rekognition is available**. The eu-central-1 (Europe/Frankfurt) region is confirmed to work. For example, the eu-north-1 failed as the service is not offered there.

**Step-by-Step AWS Setup**

All resources must be created in the **eu-central-1** region.

**1. Create IAM Role for Lambda**

This role grants the Lambda function permissions to access other AWS
services.

-   **Role Name:** LambdaRekognitionRole-Frankfurt
-   **Trusted Entity:** AWS Service - Lambda
-   **Attached Policies:**

    -   AWSLambdaBasicExecutionRole (for CloudWatch logs)
    -   AmazonS3ReadOnlyAccess (to read images from S3)
    -   AmazonRekognitionReadOnlyAccess (to call the celebrity recognition API)
    -   AWSIoTDataAccess (to publish results to MQTT)

**2. Create Amazon S3 Bucket**

This bucket will store the images uploaded from the ESP32-S3.

-   **Bucket Name:** esp32-rekognition-images-frankfurt (S3 names must be globally unique).
-   **Region:** eu-central-1
-   **Configuration:** Use default settings, keeping \"Block Public Access\" enabled.

**3. Create AWS Lambda Function**

This is the core compute logic of the backend.

- **Function Name:** celebrity-face-rekognition
- **Runtime:** Python 3.10
- **Execution Role:** Use the existing LambdaRekognitionRole-Frankfurt created above.
- **Timeout:** In Configuration \> General configuration, set the timeout to **15 seconds**.

- **Lambda Layers:** The function requires numpy and Pillow. Add the following two layers by specifying their ARN (this is the easier, clean way, adding ARNs):
    - **Numpy ARN:**
        arn:aws:lambda:eu-central-1:770693421928:layer:Klayers-p310-numpy:12
    - **Pillow ARN:**
        arn:aws:lambda:eu-central-1:770693421928:layer:Klayers-p310-Pillow:11
- **Add S3 Trigger:**
    - **Bucket:** Select the S3 bucket you created (esp32-rekognition-images-frankfurt).
    -  **Event type:** All object create events
    - **Suffix:** .bin (to ensure the function only triggers on your raw image files).
- **Lambda Function Code (lambda_function.py):**

```
Python

import boto3
import numpy as np
from PIL import Image
import io
import json

s3_client = boto3.client('s3')
rekognition_client = boto3.client('rekognition')

def lambda_handler(event, context):
    bucket_name = event['Records'][0]['s3']['bucket']['name']
    object_key = event['Records'][0]['s3']['object']['key']

    print(f"-> New file detected: '{object_key}' in bucket '{bucket_name}'")

    try:
        basename = object_key.split('/')[-1]
        parts = basename.split('_')[0].split('x')
        if len(parts) != 2:
            raise ValueError("Filename not in 'widthxheight_...' format.")

        width, height = int(parts[0]), int(parts[1])
        print(f"-> Parsed dimensions: {width}x{height}")

        response = s3_client.get_object(Bucket=bucket_name, Key=object_key)
        raw_data = response['Body'].read()

        dt = np.dtype(np.uint16)
        dt = dt.newbyteorder('>')
        rgb565_data = np.frombuffer(raw_data, dtype=dt)

        image_array = np.zeros((height, width, 3), dtype=np.uint8)
        r = (rgb565_data & 0xF800) >> 8
        g = (rgb565_data & 0x07E0) >> 3
        b = (rgb565_data & 0x001F) << 3
        image_array[:, :, 0] = r.reshape((height, width))
        image_array[:, :, 1] = g.reshape((height, width))
        image_array[:, :, 2] = b.reshape((height, width))

        img = Image.fromarray(image_array, 'RGB')
        
        buffer = io.BytesIO()
        img.save(buffer, format='PNG')
        image_bytes = buffer.getvalue()

        print("-> Calling Amazon Rekognition to find celebrities...")
        rekognition_response = rekognition_client.recognize_celebrities(
            Image={'Bytes': image_bytes}
        )

        print("-> Received Rekognition API response.")
        if 'ResponseMetadata' not in rekognition_response or rekognition_response['ResponseMetadata']['HTTPStatusCode'] != 200:
            print("Error: Rekognition API call did not return a successful 200 OK response.")
            print("Full Response:", json.dumps(rekognition_response, indent=2))
            raise ValueError("Rekognition API call failed.")

        if 'CelebrityFaces' not in rekognition_response:
            print("Warning: The 'CelebrityFaces' key is missing from the Rekognition response.")
            return {'status': 'Success', 'message': 'Response was successful but contained no celebrity data.'}

        celebrities = rekognition_response['CelebrityFaces']
        if not celebrities:
            print("No celebrities were recognized in the image.")
            return {'status': 'Success', 'message': 'No celebrities found'}

        for celebrity in celebrities:
            name = celebrity['Name']
            confidence = celebrity['MatchConfidence']
            print(f"Found celebrity: {name} with confidence {confidence:.2f}%")

        return {
            'status': 'Success',
            'celebrities_found': [celeb['Name'] for celeb in celebrities]
        }

    except Exception as e:
        print(f" An error occurred: {e}")
        raise e
```

**4. Configure AWS IoT Core**

This handles secure MQTT communication.

- **Create a Thing:** Create a \"Thing\" to represent your device (e.g., My-ESP32-S3).
- **Generate Certificates:** Create a new set of certificates.
 - **Download all files** (Device certificate, Private key, Public key, and Root CAs) and store them securely. You will embed these in the device's firmware.

- **Create and Attach a Policy:** Create a new policy and attach it to the certificate you just generated.

    - **Policy Name:** ESP32-Device-Policy
    - **Policy Document:** JSON
```    
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "*"
    },
    {
      "Effect": "Allow",
      "Action": [
        "iot:Publish",
        "iot:Subscribe",
        "iot:Receive"
      ],
      "Resource": "arn:aws:iot:eu-central-1:908027385255:topic/embed/*"
    }
  ]
}
```

- **Important:** Replace ACCOUNT_ID with your AWS Account ID.

**5. Update ESP32-S3 secrets.h**

Finally, update the secrets.h file in your ESP32-S3 server project with the endpoints and names from the resources you just created.

