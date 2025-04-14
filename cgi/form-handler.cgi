#!/bin/bash

echo "Content-Type: text/html"
echo ""

echo "<!doctype html>"
echo "<html><head><title>Form Data</title></head><body>"
echo "<h1>Form Submission Received</h1>"

echo "<h2>Environment Variables:</h2>"
echo "<pre>"
echo "REQUEST_METHOD: $REQUEST_METHOD"
echo "CONTENT_LENGTH: $CONTENT_LENGTH"
echo "CONTENT_TYPE: $CONTENT_TYPE"
echo "QUERY_STRING: $QUERY_STRING"
echo "</pre>"

if [ "$REQUEST_METHOD" = "POST" ]; then
    echo "<h2>POST Data:</h2>"
    echo "<pre>"
    cat -
    echo "</pre>"
fi

echo "<p><a href='/'>Return to form</a></p>"
echo "</body></html>"