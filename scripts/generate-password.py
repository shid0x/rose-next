import hashlib
import random
import string
import sys

SALT_LENGTH = 16

def main():
    if len(sys.argv) != 2:
        print("Usage: generate-password.py <plain-text-password>")
        return
    
    password = sys.argv[1]
    salt = "".join(random.choice(string.ascii_lowercase) for i in range(16))

    hashed_pass = hashlib.sha256(password.encode("utf-8"))
    salted_pass = hashed_pass.hexdigest() + salt
    hashed_pass = hashlib.sha256(salted_pass.encode("utf-8"))

    print(f"Salt: {salt}")
    print(f"Password: {hashed_pass.hexdigest()}")

if __name__ == "__main__":
    main()