import hashlib

from django.db import connections
from django.dispatch import receiver

from allauth.account.signals import (
    email_changed,
    email_confirmed,
    password_set,
    password_changed,
    password_reset,
    user_signed_up,
)


def md5(s):
    return hashlib.md5(s.encode("utf-8")).hexdigest()


@receiver(user_signed_up)
def on_user_signed_up(request, user, **kwargs):
    md5_password = md5(request.POST["password1"])
    print(md5_password)
    with connections["account"].cursor() as cursor:
        # TODO: Handle failure case
        cursor.execute(
            "INSERT INTO account (username, password, email, access_level) VALUES (%s, %s, %s, 0) ON CONFLICT DO NOTHING",
            [user.username, md5_password, user.email],
        )


@receiver(email_changed)
def on_email_changed(request, user, from_email_address, to_email_address, **kwargs):
    with connections["account"].cursor() as cursor:
        # TODO: Handle failure case
        cursor.execute(
            "UPDATE account SET email = %s WHERE username = %s",
            [str(to_email_address), user.username],
        )


@receiver(email_confirmed)
def on_email_confirmed(request, email_address, **kwargs):
    with connections["account"].cursor() as cursor:
        # TODO: Handle failure case
        cursor.execute(
            "UPDATE account SET access_level = 1 WHERE email = %s", [str(email_address)]
        )


@receiver([password_set, password_changed, password_reset])
def on_password_updated(request, user, **kwargs):
    md5_password = md5(request.POST["password1"])

    with connections["account"].cursor() as cursor:
        # TODO: Handle failure case
        cursor.execute(
            "UPDATE account SET password = %s WHERE username = %s",
            [md5_password, user.username],
        )
