namespace FluentMigrator.Runner.Generators
{
    public static class CompatabilityModeExtension
    {
        public static string HandleCompatabilty(this CompatabilityMode mode, string message)
        {
            if (CompatabilityMode.STRICT == mode)
            {
                throw new System.Exception(message);
            }
            return string.Empty;
        }
    }
}