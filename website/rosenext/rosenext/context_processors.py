from django.conf import settings as s
from django.contrib.sites.shortcuts import get_current_site


def settings(request):
    current_site = get_current_site(request)
    template_settings = {
        "DEBUG": s.DEBUG,
        "SITE_NAME": current_site.name,
        "DISCORD_URL": s.DISCORD_URL,
        "CLIENT_DOWNLOAD_URL": s.CLIENT_DOWNLOAD_URL,
    }
    return {"settings": template_settings}

