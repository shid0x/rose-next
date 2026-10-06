using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

namespace Map_Editor.Engine.Types
{
    /// <summary>
    /// Orthographic class.
    /// </summary>
    public class Orthographic
    {
        #region Member Declarations

        /// <summary>
        /// Gets or sets the view.
        /// </summary>
        /// <value>The view.</value>
        public Matrix View { get; private set; }

        /// <summary>
        /// Gets or sets the projection.
        /// </summary>
        /// <value>The projection.</value>
        public Matrix Projection { get; private set; }

        /// <summary>
        /// Gets or sets the position.
        /// </summary>
        /// <value>The position.</value>
        public Vector2 Position { get; private set; }

        /// <summary>
        /// Gets or sets the scale.
        /// </summary>
        /// <value>The scale.</value>
        public float Scale { get; private set; }

        /// <summary>
        /// Set by <see cref="SetTopDown"/>: the matrices stay put and the
        /// keyboard is ignored (unattended minimap shots).
        /// </summary>
        public bool Scripted { get; private set; }

        #endregion

        /// <summary>
        /// Initializes a new instance of the <see cref="Orthographic"/> class.
        /// </summary>
        public Orthographic()
        {
            View = Matrix.Identity;
            Projection = Matrix.Identity;

            Position = Vector2.Zero;
            Scale = 1.0f;
        }

        /// <summary>
        /// Update the camera matrices each frame.
        /// </summary>
        /// <param name="gameTime">Time passed since the last call to Update.</param>
        public void Update(GameTime gameTime)
        {
            if (Scripted)
                return;

            float cameraSpeed = 50.0f * (float)gameTime.ElapsedGameTime.TotalSeconds;

            KeyboardState keyboardState = Keyboard.GetState();

            Vector2 newPosition = Vector2.Zero;

            if (keyboardState.IsKeyDown(Keys.W))
                newPosition.Y += 3.0f * cameraSpeed;

            if (keyboardState.IsKeyDown(Keys.S))
                newPosition.Y -= 3.0f * cameraSpeed;

            if (keyboardState.IsKeyDown(Keys.A))
                newPosition.X -= 3.0f * cameraSpeed;

            if (keyboardState.IsKeyDown(Keys.D))
                newPosition.X += 3.0f * cameraSpeed;

            Position = newPosition;

            if (keyboardState.IsKeyDown(Keys.Space))
                Scale += 0.01f;
            else if (keyboardState.IsKeyDown(Keys.LeftControl))
                Scale -= 0.01f;

            Vector3 finalPosition = new Vector3()
            {
                X = CameraManager.PerspectiveCamera.Position.X + Position.X,
                Y = CameraManager.PerspectiveCamera.Position.Y + Position.Y,
                Z = 338.75f,
            };

            View = Matrix.Invert(Matrix.CreateTranslation(finalPosition));
            Projection = Matrix.CreateOrthographic(App.Engine.GraphicsDevice.Viewport.Width * Scale, App.Engine.GraphicsDevice.Viewport.Height * Scale, 1.0f, 100000.0f);
        }

        /// <summary>
        /// Looks straight down on the rectangle (x0, y0)-(x1, y1), editor
        /// metres, north up: the whole rectangle fills the viewport.
        /// </summary>
        public void SetTopDown(float x0, float y0, float x1, float y1)
        {
            const float EYE_HEIGHT = 4000.0f;

            Vector3 centre = new Vector3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f);
            Position = new Vector2(centre.X, centre.Y);
            View = Matrix.CreateLookAt(centre + new Vector3(0.0f, 0.0f, EYE_HEIGHT), centre, Vector3.Up);
            Projection = Matrix.CreateOrthographic(x1 - x0, y1 - y0, 1.0f, 2.0f * EYE_HEIGHT);
            Scripted = true;
        }
    }
}
