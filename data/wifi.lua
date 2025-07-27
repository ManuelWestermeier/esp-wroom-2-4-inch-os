if ConnectToWifi("io", "hhhhhh90") then
    print("WLAN verbunden!")
else
    print("WLAN-Verbindung fehlgeschlagen!")
end

msg = httpsReq({
    method = "GET",
    url = "https://manuelwestermeier.github.io/test.txt",
}).body

print("Body", msg)