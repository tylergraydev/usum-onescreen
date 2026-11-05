using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text;

namespace UsumOneScreen
{
    static class AzaharDownload
    {
        static string Setting(string name, string fallback) => Environment.GetEnvironmentVariable("USUM_AZAHAR_" + name) ?? fallback;
        static string Hash(HashAlgorithm algorithm, byte[] bytes)
        {
            using (algorithm) return BitConverter.ToString(algorithm.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }

        static void Download(string url, string destination, string label, Action<string> log)
        {
            var request = WebRequest.Create(url);
            request.Timeout = 60000;
            if (request is HttpWebRequest http) http.ReadWriteTimeout = 60000;
            using (var response = request.GetResponse())
            using (var input = response.GetResponseStream())
            using (var output = File.Create(destination))
            {
                var buffer = new byte[81920];
                long total = 0;
                int last = -10, read;
                while ((read = input.Read(buffer, 0, buffer.Length)) != 0)
                {
                    output.Write(buffer, 0, read);
                    total += read;
                    if (response.ContentLength < 1 << 20) continue;   // no percentages for small files (the licence)
                    int percent = (int)(total * 100 / response.ContentLength) / 10 * 10;
                    if (percent >= last + 10) { log($"Downloading {label}: {percent}%"); last = percent; }
                }
            }
        }

        public static void Install(Home home, Action<string> log)
        {
            Directory.CreateDirectory(home.Root);
            string temp = Path.Combine(home.Root, "azahar-download-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temp);
            try
            {
                ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                string archive = Path.Combine(temp, "azahar.zip"), license = Path.Combine(temp, "license.txt");
                log("Downloading tested Azahar 2126.2 …");
                Download(Setting("URL", "https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar-windows-msvc-2126.2.zip"), archive, "Azahar", log);
                string hash;
                using (var sha = SHA256.Create())
                using (var input = File.OpenRead(archive))
                    hash = BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "");
                if (!hash.Equals(Setting("SHA256", "4412eac8d13123c8840f11890fc26e5b0e4c76d16aa28a608941377b948a9db0"), StringComparison.OrdinalIgnoreCase))
                    throw new SetupException("Azahar SHA-256 mismatch; refusing to extract or install.");
                Download(Setting("LICENSE_URL", "https://raw.githubusercontent.com/azahar-emu/azahar/2126.2/license.txt"), license, "Azahar licence", log);
                var bytes = File.ReadAllBytes(license);
                var blob = Encoding.ASCII.GetBytes("blob " + bytes.Length + "\0").Concat(bytes).ToArray();
                if (!Hash(SHA1.Create(), blob).Equals(Setting("LICENSE_SHA1", "7b0e5ad1bddc73ac44aca523174d22ee0d0dcc56"), StringComparison.OrdinalIgnoreCase))
                    throw new SetupException("Azahar licence git blob SHA-1 mismatch; refusing to install.");
                string unpacked = Path.Combine(temp, "unpacked");
                using (var zip = ZipFile.OpenRead(archive))
                {
                    if (zip.Entries.Count(e => Path.GetFileName(e.FullName.Replace('\\', '/')).Equals("azahar.exe", StringComparison.OrdinalIgnoreCase)) != 1)
                        throw new SetupException("Expected exactly one azahar.exe in the Azahar archive.");
                    zip.ExtractToDirectory(unpacked);
                }
                string source = Path.GetDirectoryName(Directory.GetFiles(unpacked, "*", SearchOption.AllDirectories)
                    .Single(f => Path.GetFileName(f).Equals("azahar.exe", StringComparison.OrdinalIgnoreCase)));
                File.Copy(license, Path.Combine(source, "LICENSE-azahar.txt"), true);
                home.InstallAzahar(source, log);
                // The downloaded source is temporary; don't remember it for discovery.
                home.SetState("azahar_source", "");
            }
            catch (Exception e) when (e is WebException || e is IOException || e is UnauthorizedAccessException || e is InvalidDataException || e is SetupException || e is ArgumentException)
            {
                throw new SetupException("Couldn't download Azahar (no internet?). Install Azahar 2126.2 yourself, or pick its folder. " + e.Message);
            }
            finally { Directory.Delete(temp, true); }
        }
    }
}
