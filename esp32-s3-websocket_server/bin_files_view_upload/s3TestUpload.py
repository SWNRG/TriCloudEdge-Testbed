import requests
import os

def upload_to_presigned_url(presigned_url: str, file_path: str):
    """
    Uploads a file to a pre-signed S3 URL.

    Args:
        presigned_url: The pre-signed URL for the S3 upload.
        file_path: The local path to the file to be uploaded.

    Returns:
        bool: True if the upload was successful, False otherwise.
    """
    if not os.path.exists(file_path):
        print(f"Error: File not found at '{file_path}'")
        return False

    print(f"Attempting to upload '{file_path}' to the pre-signed URL...")

    # The Content-Type header is often crucial. When the pre-signed URL was
    # generated, a specific Content-Type might have been required. For .bin
    # files, 'application/octet-stream' is a common and safe choice.
    # If the URL was generated with a different Content-Type, you must use
    # that exact same type here.
    headers = {
        'Content-Type': 'application/octet-stream'
    }

    try:
        # Read the file in binary mode
        with open(file_path, 'rb') as f:
            file_content = f.read()

        # Perform the PUT request
        response = requests.put(presigned_url, data=file_content, headers=headers)

        # Check the response
        if response.status_code == 200:
            print("\n✅ Upload successful!")
            print("The file was uploaded to S3.")
            return True
        else:
            print(f"\n❌ Upload failed with status code: {response.status_code}")
            print("Response from server:")
            # The response text from S3 often contains an XML error message
            # with details about what went wrong (e.g., SignatureDoesNotMatch).
            print(response.text)
            return False

    except requests.exceptions.RequestException as e:
        print(f"\n❌ An error occurred during the request: {e}")
        return False

def create_dummy_bin_file(file_name="test.bin", size_kb=1):
    """Creates a small binary file for testing purposes."""
    if not os.path.exists(file_name):
        print(f"Creating a dummy binary file named '{file_name}'...")
        with open(file_name, 'wb') as f:
            f.write(os.urandom(1024 * size_kb))
    else:
        print(f"Dummy file '{file_name}' already exists.")


if __name__ == "__main__":
    # --- Configuration ---

    # 1. Paste your pre-signed URL here.
    # IMPORTANT: Make sure the URL is wrapped in quotes.
    S3_PRESIGNED_URL = "PASTE THE URL CREATED BY S3 BUCKET" # Be careful, it has to be FRESH!
    # 2. Specify the path to your .bin file.
    # We'll create a dummy file for you, but you can change this to your actual file.
    FILE_TO_UPLOAD = "111x133_782016807.bin"

    # --- End of Configuration ---

    # Create a dummy file to test with
    create_dummy_bin_file(FILE_TO_UPLOAD)

    if "PASTE_YOUR_PRE-SIGNED_URL_HERE" in S3_PRESIGNED_URL:
        print("="*60)
        print("🛑 Please edit the script and replace 'PASTE_YOUR_PRE-SIGNED_URL_HERE'")
        print("   with your actual S3 pre-signed URL.")
        print("="*60)
    else:
        upload_to_presigned_url(S3_PRESIGNED_URL, FILE_TO_UPLOAD)

