from django.contrib import admin
from django.urls import include, path
from django.views.generic import TemplateView

import oauth2_provider.views as oauth2_views

import apps.user

oauth2_endpoint_views = [
    path("authorize/", oauth2_views.AuthorizationView.as_view(), name="authorize"),
    path("token/", oauth2_views.TokenView.as_view(), name="token"),
    path("revoke-token/", oauth2_views.RevokeTokenView.as_view(), name="revoke-token"),
]

urlpatterns = [
    path("", TemplateView.as_view(template_name="home.html")),
    path("account/", include("allauth.urls")),
    path("admin/", admin.site.urls),
    path("oauth/", include((oauth2_endpoint_views, "oauth"))),
    path("user/", include("apps.user.urls", namespace="user")),
]
