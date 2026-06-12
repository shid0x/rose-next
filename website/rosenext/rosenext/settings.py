import os
from django.contrib.messages import constants as messages

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SECRET_KEY = os.environ.get(
    "SECRET_KEY", "0i#s3)#ije8e_pjs(w%=13fglngikvwm3%+34y&(ivx6+4q^n8"
)

DEBUG = os.environ.get("DEBUG", "false").lower() == "true"

ALLOWED_HOSTS = ["rosenext.com"]
if DEBUG:
    ALLOWED_HOSTS += ["127.0.0.1", "localhost", "192.168.1.100"]


INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.sites",
    "django.contrib.staticfiles",
    # Apps
    "apps.account",
    "apps.user",
    # Thirdparty apps
    "allauth",
    "allauth.account",
    "allauth.socialaccount",
    "oauth2_provider",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
    "oauth2_provider.middleware.OAuth2TokenMiddleware",
]

ROOT_URLCONF = "rosenext.urls"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [os.path.join(BASE_DIR, "templates")],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.debug",
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
                "rosenext.context_processors.settings",
            ],
        },
    },
]

WSGI_APPLICATION = "rosenext.wsgi.application"

SITE_ID = 1

# Django message tags to bulma tags
MESSAGE_TAGS = {
    messages.DEBUG: "is-link",
    messages.INFO: "is-info",
    messages.SUCCESS: "is-success",
    messages.WARNING: "is-warning",
    messages.ERROR: "is-danger",
}


# Database

DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": os.environ.get("WEBSITE_DATABASE_NAME", "rose-next-website"),
        "USER": os.environ.get("WEBSITE_DATABASE_USERNAME", "postgres"),
        "PASSWORD": os.environ.get("WEBSITE_DATABASE_PASSWORD", "postgres"),
        "HOST": os.environ.get("WEBSITE_DATABASE_HOST", "host.docker.internal"),
        "PORT": os.environ.get("WEBSITE_DATABASE_PORT", 5432),
    },
    "account": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": os.environ.get("ACCOUNT_DATABASE_NAME", "rose-next-account"),
        "USER": os.environ.get("ACCOUNT_DATABASE_USERNAME", "postgres"),
        "PASSWORD": os.environ.get("ACCOUNT_DATABASE_PASSWORD", "postgres"),
        "HOST": os.environ.get("ACCOUNT_DATABASE_HOST", "host.docker.internal"),
        "PORT": os.environ.get("ACCOUNT_DATABASE_PORT", 5432),
    },
    "game": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": os.environ.get("GAME_DATABASE_NAME", "rose-next"),
        "USER": os.environ.get("GAME_DATABASE_USERNAME", "postgres"),
        "PASSWORD": os.environ.get("GAME_DATABASE_PASSWORD", "postgres"),
        "HOST": os.environ.get("GAME_DATABASE_HOST", "host.docker.internal"),
        "PORT": os.environ.get("GAME_DATABASE_PORT", 5432),
    },
}

# Auth
AUTH_USER_MODEL = "user.RoseNextUser"

AUTHENTICATION_BACKENDS = [
    "django.contrib.auth.backends.ModelBackend",
    "oauth2_provider.backends.OAuth2Backend",
]

AUTH_PASSWORD_VALIDATORS = [
    {
        "NAME": "django.contrib.auth.password_validation.UserAttributeSimilarityValidator",
    },
    {"NAME": "django.contrib.auth.password_validation.MinimumLengthValidator",},
    {"NAME": "django.contrib.auth.password_validation.CommonPasswordValidator",},
    {"NAME": "django.contrib.auth.password_validation.NumericPasswordValidator",},
]

LOGIN_URL = "/account/login"
LOGIN_REDIRECT_URL = "/"

# Internationalization
LANGUAGE_CODE = "en-us"
TIME_ZONE = "UTC"
USE_I18N = True
USE_L10N = True
USE_TZ = True

STATIC_URL = "/static/"
STATICFILES_DIRS = [os.path.join(BASE_DIR, "static")]

# Email
EMAIL_HOST = os.environ.get("EMAIL_SMTP_HOST", "smtp.fastmail.com")
EMAIL_PORT = os.environ.get("EMAIL_SMTP_PORT", 465)
EMAIL_HOST_USER = os.environ.get("EMAIL_SMTP_USERNAME", "info@rosenext.com")
EMAIL_HOST_PASSWORD = os.environ.get("EMAIL_SMTP_PASSWORD", "")
EMAIL_SUBJECT_PREFIX = "[ROSE Next] "
EMAIL_USE_SSL = True
SERVER_EMAIL = "info@rosenext.com"
DEFAULT_FROM_EMAIL = "info@rosenext.com"

# django-allauth configuration
ACCOUNT_AUTHENTICATION_METHOD = "username_email"
ACCOUNT_EMAIL_REQUIRED = True
ACCOUNT_EMAIL_VERIFICATION = "mandatory"

# ROSE Next config
CLIENT_DOWNLOAD_URL = "https://www.rosenext.com/game/latest.zip"
DISCORD_URL = "https://discord.gg/syW7RBP"
