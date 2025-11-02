using System;

namespace Themis.PIIManager.Models
{
    // Lokale Kopie (nicht verwendet) – Vermeidung von Namenskonflikten mit Shared Models
    internal class PiiMappingLocal
    {
        public string OriginalUuid { get; set; } = string.Empty;
        public string Pseudonym { get; set; } = string.Empty;
        public DateTimeOffset CreatedAt { get; set; }
        public DateTimeOffset UpdatedAt { get; set; }
        public bool Active { get; set; }
    }
}