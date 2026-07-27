using WebScene.Core;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssArrangementEngineTests
{
    [Fact]
    public void BlockArrangementSeparatesContentPaddingBorderAndMarginBoxes()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Padding = CssLayoutEdges.All(CssLayoutLength.Pixels(10)),
            Border = CssLayoutEdges.All(CssLayoutLength.Pixels(2))
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(20),
            Padding = CssLayoutEdges.All(CssLayoutLength.Pixels(5)),
            Border = CssLayoutEdges.All(CssLayoutLength.Pixels(1)),
            Margin = CssLayoutEdges.All(CssLayoutLength.Pixels(3))
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 100));

        Assert.Equal(new WebSceneRect(15, 15, 62, 32), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(21, 21, 50, 20), snapshot[2].ContentBox);
        Assert.Equal(new WebSceneRect(12, 12, 68, 38), snapshot[2].MarginBox);
    }

    [Fact]
    public void InlineItemsWrapAndRelativeOffsetDoesNotChangeFollowingFlow()
    {
        var root = new CssLayoutNode(1);
        root.Add(Inline(2, 60, 10));
        root.Add(Inline(3, 60, 12, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.InlineBlock,
            Position = CssLayoutPosition.Relative,
            Left = CssLayoutLength.Pixels(5),
            Width = CssLayoutLength.Pixels(60),
            Height = CssLayoutLength.Pixels(12)
        }));
        root.Add(Inline(4, 20, 8));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 100));

        Assert.Equal(new WebSceneRect(0, 0, 60, 10), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(5, 10, 60, 12), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(60, 10, 20, 8), snapshot[4].BorderBox);
    }

    [Fact]
    public void CollapsedWhitespaceBetweenInlineSiblingsOffsetsFollowingFragment()
    {
        var root = new CssLayoutNode(1);
        root.Add(Inline(2, 20, 10));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle { Display = CssLayoutDisplay.Inline })
        {
            IsText = true,
            IsCollapsibleWhitespace = true,
            CollapsedWhitespaceWidth = 5
        });
        root.Add(Inline(4, 20, 10));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 20));

        Assert.Equal(new WebSceneRect(0, 0, 20, 10), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(25, 0, 20, 10), snapshot[4].BorderBox);
    }

    [Fact]
    public void FlexGrowJustificationAlignmentAndOrderArePortable()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            AlignItems = CssLayoutAlignment.Center,
            JustifyContent = CssLayoutJustifyContent.SpaceBetween,
            ColumnGap = CssLayoutLength.Pixels(10)
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(40), Height = CssLayoutLength.Pixels(20), Order = 2
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(40), Height = CssLayoutLength.Pixels(10), Order = 1
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 60));

        Assert.Equal(new WebSceneRect(0, 25, 40, 10), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(160, 20, 40, 20), snapshot[2].BorderBox);
    }

    [Fact]
    public void FlexBaselineAlignsTextAndSynthesizedNonTextBaselines()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            AlignItems = CssLayoutAlignment.Baseline
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(30)
        })
        {
            IntrinsicSize = new WebSceneSize(30, 15),
            FirstBaseline = 13
        });
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Height = CssLayoutLength.Pixels(7),
            FlexGrow = 1
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(360, 15));

        Assert.Equal(new WebSceneRect(0, 0, 30, 15), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(30, 6, 330, 7), snapshot[3].BorderBox);
    }

    [Fact]
    public void BlockAfterInlineRunStartsBelowCompletedLine()
    {
        var root = new CssLayoutNode(1);
        root.Add(Inline(2, 30, 12));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(20)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 100));

        Assert.Equal(new WebSceneRect(0, 12, 50, 20), snapshot[3].BorderBox);
    }

    [Fact]
    public void ZeroHeightLineBoxCentersTextOverflowWithoutAdvancingFlow()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Inline
        })
        {
            IsText = true,
            HasZeroLineHeight = true,
            IntrinsicSize = new WebSceneSize(120, 96)
        });
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(20),
            Height = CssLayoutLength.Pixels(10)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 120));

        Assert.Equal(new WebSceneRect(0, -48, 120, 96), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(0, 0, 20, 10), snapshot[3].BorderBox);
    }

    [Fact]
    public void FlexGrowAndShrinkDistributeAvailableMainSpace()
    {
        var growRoot = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Flex });
        growRoot.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            FlexBasis = CssLayoutLength.Pixels(20), FlexGrow = 1, Height = CssLayoutLength.Pixels(10)
        }));
        growRoot.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            FlexBasis = CssLayoutLength.Pixels(20), FlexGrow = 3, Height = CssLayoutLength.Pixels(10)
        }));
        var engine = new CssArrangementEngine();

        var grown = engine.Arrange(growRoot, new WebSceneSize(100, 20));

        Assert.Equal(35, grown[2].BorderBox.Width);
        Assert.Equal(65, grown[3].BorderBox.Width);

        var shrinkRoot = new CssLayoutNode(10, new CssLayoutStyle { Display = CssLayoutDisplay.Flex });
        shrinkRoot.Add(new CssLayoutNode(11, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(80), Height = CssLayoutLength.Pixels(10)
        }));
        shrinkRoot.Add(new CssLayoutNode(12, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(80), Height = CssLayoutLength.Pixels(10)
        }));

        var shrunk = engine.Arrange(shrinkRoot, new WebSceneSize(100, 20));

        Assert.Equal(50, shrunk[11].BorderBox.Width);
        Assert.Equal(50, shrunk[12].BorderBox.Width);
    }

    [Fact]
    public void WrappedFlexResolvesEachLineAndStretchesCrossLinesIndependently()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            FlexWrap = CssLayoutFlexWrap.Wrap,
            ColumnGap = CssLayoutLength.Pixels(5),
            RowGap = CssLayoutLength.Pixels(4)
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(70),
            FlexGrow = 1
        }) { IntrinsicSize = new WebSceneSize(0, 10) });
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(40),
            FlexGrow = 1
        }) { IntrinsicSize = new WebSceneSize(0, 10) });
        root.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(40),
            FlexGrow = 1
        }) { IntrinsicSize = new WebSceneSize(0, 10) });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 40));

        Assert.Equal(new WebSceneRect(0, 0, 100, 18), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(0, 22, 47.5, 18), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(52.5, 22, 47.5, 18), snapshot[4].BorderBox);
    }

    [Fact]
    public void WrapReversePlacesTheFirstFlexLineAtTheCrossEnd()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            FlexWrap = CssLayoutFlexWrap.WrapReverse,
            RowGap = CssLayoutLength.Pixels(4)
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle { Width = CssLayoutLength.Pixels(70) })
        {
            IntrinsicSize = new WebSceneSize(0, 10)
        });
        root.Add(new CssLayoutNode(3, new CssLayoutStyle { Width = CssLayoutLength.Pixels(40) })
        {
            IntrinsicSize = new WebSceneSize(0, 10)
        });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 40));

        Assert.Equal(new WebSceneRect(0, 22, 70, 18), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(0, 0, 40, 18), snapshot[3].BorderBox);
    }

    [Fact]
    public void FlexMainAxisAutoMarginAbsorbsRemainingSpace()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(34),
            FlexShrink = 0
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(130),
            Height = CssLayoutLength.Pixels(34),
            FlexShrink = 0,
            Margin = new CssLayoutEdges(
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Auto)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(340, 34));

        Assert.Equal(new WebSceneRect(0, 0, 100, 34), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(210, 0, 130, 34), snapshot[3].BorderBox);
    }

    [Fact]
    public void ColumnReverseFlexSupportsStretchAndFlexEndAlignment()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            FlexDirection = CssLayoutFlexDirection.ColumnReverse,
            AlignItems = CssLayoutAlignment.Stretch
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Height = CssLayoutLength.Pixels(20)
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(30),
            Height = CssLayoutLength.Pixels(10),
            AlignSelf = CssLayoutAlignment.FlexEnd
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 60));

        Assert.Equal(new WebSceneRect(70, 0, 30, 10), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(0, 10, 100, 20), snapshot[2].BorderBox);
    }

    [Fact]
    public void GridUsesIntrinsicExplicitAndRelativeGeometry()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.InlineGrid });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Margin = CssLayoutEdges.All(CssLayoutLength.Pixels(2))
        }) { IntrinsicSize = new WebSceneSize(20, 10) });
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Relative,
            Left = CssLayoutLength.Pixels(3),
            Width = CssLayoutLength.Pixels(30),
            Height = CssLayoutLength.Pixels(12)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 40));

        Assert.Equal(new WebSceneRect(2, 2, 20, 10), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(27, 0, 30, 12), snapshot[3].BorderBox);
    }

    [Fact]
    public void AutoFractionGridPlacesPairsInRowsAndHonorsFullWidthSpans()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.InlineGrid,
            GridTemplateColumns = "auto 1fr"
        });
        root.Add(new CssLayoutNode(2) { IntrinsicSize = new WebSceneSize(80, 34) });
        root.Add(new CssLayoutNode(3) { IntrinsicSize = new WebSceneSize(100, 34) });
        root.Add(new CssLayoutNode(4, new CssLayoutStyle { GridColumn = "1 / 3" })
        {
            IntrinsicSize = new WebSceneSize(160, 18)
        });
        root.Add(new CssLayoutNode(5) { IntrinsicSize = new WebSceneSize(70, 50) });
        root.Add(new CssLayoutNode(6) { IntrinsicSize = new WebSceneSize(90, 50) });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 102));

        Assert.Equal(new WebSceneRect(0, 0, 80, 34), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(80, 0, 120, 34), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(0, 34, 200, 18), snapshot[4].BorderBox);
        Assert.Equal(new WebSceneRect(0, 52, 80, 50), snapshot[5].BorderBox);
        Assert.Equal(new WebSceneRect(80, 52, 120, 50), snapshot[6].BorderBox);
    }

    [Fact]
    public void FixedPixelGridTracksStretchAutoChildrenAndIncludeColumnGap()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Grid,
            GridTemplateColumns = "150px 100px",
            ColumnGap = CssLayoutLength.Pixels(12)
        });
        root.Add(new CssLayoutNode(2) { IntrinsicSize = new WebSceneSize(40, 28) });
        root.Add(new CssLayoutNode(3) { IntrinsicSize = new WebSceneSize(20, 28) });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(262, 28));

        Assert.Equal(new WebSceneRect(0, 0, 150, 28), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(162, 0, 100, 28), snapshot[3].BorderBox);
    }

    [Theory]
    [InlineData(CssLayoutJustifyContent.FlexEnd, 60)]
    [InlineData(CssLayoutJustifyContent.Center, 30)]
    [InlineData(CssLayoutJustifyContent.SpaceAround, 15)]
    [InlineData(CssLayoutJustifyContent.SpaceEvenly, 20)]
    public void FlexJustificationOffsetsArePortable(CssLayoutJustifyContent justification, double expectedX)
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            JustifyContent = justification
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(20), Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(20), Height = CssLayoutLength.Pixels(10)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 20));

        Assert.Equal(expectedX, snapshot[2].BorderBox.X);
    }

    [Fact]
    public void EmptyFlexContainerArrangesWithoutChildren()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.InlineFlex });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(10, 10));

        Assert.Single(snapshot.Boxes);
    }

    [Fact]
    public void AbsoluteUsesPositionedAncestorWhileFixedUsesViewport()
    {
        var root = new CssLayoutNode(1);
        var relative = new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Relative,
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(50)
        });
        relative.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Right = CssLayoutLength.Pixels(5),
            Bottom = CssLayoutLength.Pixels(5),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        relative.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Fixed,
            Right = CssLayoutLength.Pixels(5),
            Bottom = CssLayoutLength.Pixels(5),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(relative);

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(300, 200));

        Assert.Equal(new WebSceneRect(85, 35, 10, 10), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(285, 185, 10, 10), snapshot[4].BorderBox);
    }

    [Fact]
    public void PositionedMarginsOffsetAutoAndDeclaredInsetEdges()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(20),
            Margin = new CssLayoutEdges(
                CssLayoutLength.Pixels(12),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(500))
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Right = CssLayoutLength.Pixels(10),
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(10),
            Margin = new CssLayoutEdges(
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(7),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0))
        }));
        root.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Left = CssLayoutLength.Pixels(10),
            Right = CssLayoutLength.Pixels(20),
            Height = CssLayoutLength.Pixels(10),
            Margin = new CssLayoutEdges(
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(7),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(5))
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 100));

        Assert.Equal(new WebSceneRect(500, 12, 100, 20), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(133, 0, 50, 10), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(15, 0, 158, 10), snapshot[4].BorderBox);
    }

    [Fact]
    public void BlockAbsoluteAutoInsetsUseSourceOrderStaticPositionWithoutConsumingFlow()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Height = CssLayoutLength.Pixels(12)
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(new CssLayoutNode(5, new CssLayoutStyle
        {
            Height = CssLayoutLength.Pixels(8)
        }));

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 100));

        Assert.Equal(12, snapshot[3].BorderBox.Y);
        Assert.Equal(12, snapshot[4].BorderBox.Y);
        Assert.Equal(12, snapshot[5].BorderBox.Y);
    }

    [Fact]
    public void AbsoluteStaticPositionComesFromStaticParentWhileContainingBlockIsAncestor()
    {
        var root = new CssLayoutNode(1);
        var containingBlock = new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Relative,
            Width = CssLayoutLength.Pixels(200),
            Height = CssLayoutLength.Pixels(200)
        });
        var staticParent = new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(200),
            Height = CssLayoutLength.Pixels(200)
        });
        staticParent.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Height = CssLayoutLength.Pixels(100)
        }));
        staticParent.Add(new CssLayoutNode(5, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Width = CssLayoutLength.Pixels(200),
            Height = CssLayoutLength.Pixels(100)
        }));
        containingBlock.Add(staticParent);
        root.Add(containingBlock);

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(300, 300));

        Assert.Equal(new WebSceneRect(0, 100, 200, 100), snapshot[5].BorderBox);
    }

    [Fact]
    public void AbsoluteContainingBlockUsesPositionedAncestorsPaddingBox()
    {
        var root = new CssLayoutNode(1);
        var relative = new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Relative,
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(50),
            Padding = new CssLayoutEdges(
                CssLayoutLength.Pixels(4),
                CssLayoutLength.Pixels(4),
                CssLayoutLength.Pixels(4),
                CssLayoutLength.Pixels(4))
        });
        relative.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Left = CssLayoutLength.Pixels(0),
            Top = CssLayoutLength.Pixels(0),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        relative.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Right = CssLayoutLength.Pixels(0),
            Bottom = CssLayoutLength.Pixels(0),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(relative);

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(300, 200));

        Assert.Equal(new WebSceneRect(0, 0, 10, 10), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(98, 48, 10, 10), snapshot[4].BorderBox);
        Assert.Equal(new WebSceneRect(4, 4, 100, 50), snapshot[2].ContentBox);
    }

    [Fact]
    public void ProjectedSubtreeUsesExternalAbsoluteAndFixedContainingBlocks()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute,
            Right = CssLayoutLength.Pixels(5),
            Bottom = CssLayoutLength.Pixels(5),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Fixed,
            Right = CssLayoutLength.Pixels(5),
            Bottom = CssLayoutLength.Pixels(5),
            Width = CssLayoutLength.Pixels(10),
            Height = CssLayoutLength.Pixels(10)
        }));

        var snapshot = new CssArrangementEngine().Arrange(
            root,
            new WebSceneSize(100, 50),
            new WebSceneRect(-20, -10, 300, 200),
            new WebSceneRect(-50, -30, 500, 400));

        Assert.Equal(new WebSceneRect(265, 175, 10, 10), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(435, 355, 10, 10), snapshot[3].BorderBox);
    }

    [Fact]
    public void HiddenNodesGetEmptyGeometryAndInvalidViewportFails()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle { Display = CssLayoutDisplay.None }));
        var engine = new CssArrangementEngine();

        Assert.Equal(WebSceneRect.Empty, engine.Arrange(root, new WebSceneSize(10, 10))[2].BorderBox);
        Assert.Throws<ArgumentOutOfRangeException>(() => engine.Arrange(root, new WebSceneSize(-1, 10)));
        Assert.Throws<ArgumentOutOfRangeException>(() => engine.Arrange(
            root,
            new WebSceneSize(10, 10),
            new WebSceneRect(double.NaN, 0, 10, 10),
            new WebSceneRect(0, 0, 10, 10)));
        Assert.Throws<ArgumentOutOfRangeException>(() => new CssLayoutNode(0));
        Assert.Throws<InvalidOperationException>(() => root.Add(root));

        var duplicate = new CssLayoutNode(3);
        duplicate.Add(new CssLayoutNode(4));
        duplicate.Add(new CssLayoutNode(4));
        Assert.Throws<ArgumentException>(() => engine.Arrange(duplicate, new WebSceneSize(10, 10)));
    }

    [Fact]
    public void TableArrangementAlignsCellsToSharedColumnsWithoutOverlap()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        var body = new CssLayoutNode(2, new CssLayoutStyle { Display = CssLayoutDisplay.TableRowGroup });
        var firstRow = new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableRow,
            Height = CssLayoutLength.Pixels(32),
            VerticalAlign = CssLayoutVerticalAlign.Middle
        });
        firstRow.Add(new CssLayoutNode(4, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell })
            { IntrinsicSize = new WebSceneSize(36, 20) });
        firstRow.Add(new CssLayoutNode(5, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell })
            { IntrinsicSize = new WebSceneSize(80, 18) });
        var secondRow = new CssLayoutNode(6, new CssLayoutStyle { Display = CssLayoutDisplay.TableRow });
        secondRow.Add(new CssLayoutNode(7, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell })
            { IntrinsicSize = new WebSceneSize(6, 13) });
        secondRow.Add(new CssLayoutNode(8, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell })
            { IntrinsicSize = new WebSceneSize(218, 13) });
        body.Add(firstRow);
        body.Add(secondRow);
        table.Add(body);

        var snapshot = new CssArrangementEngine().Arrange(table, new WebSceneSize(254, 45));

        Assert.Equal(new WebSceneRect(0, 0, 254, 32), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(0, 32, 254, 13), snapshot[6].BorderBox);
        Assert.Equal(new WebSceneRect(0, 0, 36, 32), snapshot[4].BorderBox);
        Assert.Equal(new WebSceneRect(36, 0, 218, 32), snapshot[5].BorderBox);
        Assert.Equal(new WebSceneRect(0, 32, 36, 13), snapshot[7].BorderBox);
        Assert.Equal(new WebSceneRect(36, 32, 218, 13), snapshot[8].BorderBox);
    }

    [Fact]
    public void TableArrangementGeneratesImplicitRowForDirectCells()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        table.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(3.6)
        }) { IntrinsicSize = new WebSceneSize(0, 20) });
        table.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(3.6)
        }) { IntrinsicSize = new WebSceneSize(0, 20) });

        var snapshot = new CssArrangementEngine().Arrange(table, new WebSceneSize(7.2, 20));

        Assert.Equal(new WebSceneRect(0, 0, 3.6, 20), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(3.6, 0, 3.6, 20), snapshot[3].BorderBox);
    }

    [Fact]
    public void TableArrangementGeneratesAnonymousWrappersForImproperFlexChild()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        table.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            Width = CssLayoutLength.Pixels(240),
            Height = CssLayoutLength.Pixels(38)
        }));

        var snapshot = new CssArrangementEngine().Arrange(table, new WebSceneSize(240, 38));

        Assert.Equal(new WebSceneRect(0, 0, 240, 38), snapshot[2].BorderBox);
    }

    [Fact]
    public void DefiniteSingleColumnTableDistributesExcessToAnonymousAutoFlexChild()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        var inner = new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            Height = CssLayoutLength.Pixels(40)
        }) { IntrinsicSize = new WebSceneSize(200, 40) };
        inner.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(120),
            Height = CssLayoutLength.Pixels(40)
        }));
        inner.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            FlexGrow = 1,
            FlexBasis = CssLayoutLength.Pixels(0),
            Height = CssLayoutLength.Pixels(40)
        }));
        inner.Add(new CssLayoutNode(5, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(80),
            Height = CssLayoutLength.Pixels(40)
        }));
        table.Add(inner);

        var snapshot = new CssArrangementEngine().Arrange(table, new WebSceneSize(1000, 40));

        Assert.Equal(new WebSceneRect(0, 0, 1000, 40), snapshot[2].BorderBox);
        Assert.Equal(new WebSceneRect(920, 0, 80, 40), snapshot[5].BorderBox);
    }

    [Fact]
    public void FixedTableDistributesExcessProportionallyToNonzeroLengthColumns()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Table,
            FixedTableLayout = true,
            Width = CssLayoutLength.Pixels(300)
        });
        var row = new CssLayoutNode(2, new CssLayoutStyle { Display = CssLayoutDisplay.TableRow });
        row.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(20)
        }));
        row.Add(new CssLayoutNode(4, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(10)
        }));
        row.Add(new CssLayoutNode(5, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Percent(10)
        }));
        table.Add(row);

        var snapshot = new CssArrangementEngine().Arrange(table, new WebSceneSize(300, 20));

        Assert.Equal(new WebSceneRect(0, 0, 180, 20), snapshot[3].BorderBox);
        Assert.Equal(new WebSceneRect(180, 0, 90, 20), snapshot[4].BorderBox);
        Assert.Equal(new WebSceneRect(270, 0, 30, 20), snapshot[5].BorderBox);
    }

    [Fact]
    public void TableColumnAndImproperColumnGroupChildrenHaveNoLayoutBoxes()
    {
        var root = new CssLayoutNode(1);
        var column = new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableColumn
        });
        column.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(100)
        }));
        var columnGroup = new CssLayoutNode(4, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableColumnGroup
        });
        columnGroup.Add(new CssLayoutNode(5, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(100)
        }));
        root.Add(column);
        root.Add(columnGroup);

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(200, 100));

        Assert.Equal(WebSceneRect.Empty, snapshot[2].BorderBox);
        Assert.Equal(WebSceneRect.Empty, snapshot[3].BorderBox);
        Assert.Equal(WebSceneRect.Empty, snapshot[4].BorderBox);
        Assert.Equal(WebSceneRect.Empty, snapshot[5].BorderBox);
    }

    private static CssLayoutNode Inline(long id, double width, double height, CssLayoutStyle? style = null)
        => new(id, style ?? new CssLayoutStyle
        {
            Display = CssLayoutDisplay.InlineBlock,
            Width = CssLayoutLength.Pixels(width),
            Height = CssLayoutLength.Pixels(height)
        });
}
