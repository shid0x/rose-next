from django.apps import AppConfig


class AccountConfig(AppConfig):
    name = "apps.account"
    label = "rosenext_account"

    def ready(self):
        import apps.account.signals
