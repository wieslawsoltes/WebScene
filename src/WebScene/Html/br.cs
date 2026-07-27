using System;
using Avalonia.Controls.Documents;

namespace WebScene;

public class br : LineBreak
{
    protected override Type StyleKeyOverride => typeof(LineBreak);
}
