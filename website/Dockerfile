FROM python:3.8

RUN apt-get update

WORKDIR /app
COPY . .
RUN pip install -r requirements.txt

EXPOSE 8000
CMD ["python", "rosenext/manage.py", "runserver", "0.0.0.0:8000"]