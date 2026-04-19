using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Threading.Tasks;
using System.Web;

namespace Labs.Engine.Psn;

/// <summary>
/// PSN OAuth flow for obtaining the 8-byte `psn_account_id` needed for PS5
/// Remote Play pairing. Endpoints mirror chiaki-ng's `psn-account-id.py`.
///
/// In-app browser flow:
///   1. App navigates an embedded WebView2 to <see cref="AuthorizeUrl"/>.
///   2. User signs in. Sony redirects to <see cref="RedirectPrefix"/>?code=XXX.
///   3. App detects the redirect, extracts the code, and calls <see cref="ExchangeAsync"/>.
/// </summary>
public static class PsnAuth
{
    private const string ClientId = "ba495a24-818c-472b-b12d-ff231c1b5745";
    private const string ClientSecret = "mvaiZkRsAsI1IBkY";

    public const string RedirectPrefix = "https://remoteplay.dl.playstation.net/remoteplay/redirect";

    public static readonly string AuthorizeUrl =
        "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize" +
        "?service_entity=urn:service-entity:psn" +
        "&response_type=code" +
        "&client_id=" + ClientId +
        "&redirect_uri=" + Uri.EscapeDataString(RedirectPrefix) +
        "&scope=psn:clientapp" +
        "&request_locale=en_US" +
        "&ui=pr" +
        "&service_logo=ps" +
        "&layout_type=popup" +
        "&smcid=remoteplay" +
        "&prompt=always" +
        "&PlatformPrivacyWs1=minimal";

    private const string TokenUrl = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";

    public static string? ExtractCode(string redirectedUrl)
    {
        if (string.IsNullOrWhiteSpace(redirectedUrl)) return null;
        try
        {
            var uri = new Uri(redirectedUrl);
            var q = HttpUtility.ParseQueryString(uri.Query);
            return q["code"];
        }
        catch { return null; }
    }

    /// <summary>Exchanges an auth code for the PSN account id (base64 of 8 little-endian bytes).</summary>
    public static async Task<string> ExchangeAsync(string code)
    {
        using var http = new HttpClient();
        http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Basic",
            Convert.ToBase64String(System.Text.Encoding.ASCII.GetBytes($"{ClientId}:{ClientSecret}")));

        using var form = new FormUrlEncodedContent(new Dictionary<string, string>
        {
            ["grant_type"]   = "authorization_code",
            ["code"]         = code,
            ["redirect_uri"] = RedirectPrefix,
        });
        using var tokResp = await http.PostAsync(TokenUrl, form);
        var tokJson = await tokResp.Content.ReadAsStringAsync();
        if (!tokResp.IsSuccessStatusCode)
            throw new InvalidOperationException($"Token exchange failed: {(int)tokResp.StatusCode} {tokResp.ReasonPhrase}\n{tokJson}");

        using var tokDoc = JsonDocument.Parse(tokJson);
        var accessToken = tokDoc.RootElement.GetProperty("access_token").GetString()
            ?? throw new InvalidOperationException("No access_token in PSN response.");

        // Info endpoint: TOKEN_URL + "/" + access_token, same basic auth.
        using var infoReq = new HttpRequestMessage(HttpMethod.Get, $"{TokenUrl}/{Uri.EscapeDataString(accessToken)}");
        using var infoResp = await http.SendAsync(infoReq);
        var infoJson = await infoResp.Content.ReadAsStringAsync();
        if (!infoResp.IsSuccessStatusCode)
            throw new InvalidOperationException($"Account info failed: {(int)infoResp.StatusCode} {infoResp.ReasonPhrase}\n{infoJson}");

        using var infoDoc = JsonDocument.Parse(infoJson);
        if (!infoDoc.RootElement.TryGetProperty("user_id", out var uid))
            throw new InvalidOperationException($"PSN response missing user_id:\n{infoJson}");

        var decimalStr = uid.ValueKind == JsonValueKind.String ? uid.GetString()! : uid.GetRawText();
        if (!ulong.TryParse(decimalStr, out var id))
            throw new InvalidOperationException($"Unexpected user_id format: {decimalStr}");

        var bytes = BitConverter.GetBytes(id); // little-endian on x86/x64
        return Convert.ToBase64String(bytes);
    }
}
