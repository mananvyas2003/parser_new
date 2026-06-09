using System.Diagnostics;
using System.Text;

var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

app.MapPost("/api/upload", async (HttpContext context) =>
{
    if (!context.Request.HasFormContentType || context.Request.Form.Files.Count == 0)
        return Results.BadRequest(new { error = "No file uploaded." });

    var file = context.Request.Form.Files[0];
    string parserPath = Path.Combine(AppContext.BaseDirectory, "parsernewb.exe");

    if (!File.Exists(parserPath))
        return Results.Problem("Native parser engine binary (parsernewb.exe) not found in the server root.");

    try
    {
        using var ms = new MemoryStream();
        await file.CopyToAsync(ms);
        byte[] fileBytes = ms.ToArray();

        var psi = new ProcessStartInfo
        {
            FileName = parserPath,
            Arguments = "--stream",
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        using var process = new Process { StartInfo = psi };
        process.Start();

        using (var stdin = process.StandardInput.BaseStream)
        {
            await stdin.WriteAsync(fileBytes, 0, fileBytes.Length);
            await stdin.FlushAsync();
        }

        string jsonOutput = await process.StandardOutput.ReadToEndAsync();
        await process.WaitForExitAsync();

        if (process.ExitCode != 0)
        {
            string err = await process.StandardError.ReadToEndAsync();
            return Results.Problem($"Parser Engine Error: {err}");
        }

        return Results.Text(jsonOutput, "application/json", Encoding.UTF8);
    }
    catch (Exception ex)
    {
        return Results.Problem($"Server processing failure: {ex.Message}");
    }
});

app.Run("http://localhost:5000");